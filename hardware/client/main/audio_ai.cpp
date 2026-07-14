extern "C" {
#include "audio_ai.h"
#include "mic.h"
}

#include <math.h>
#include <string.h>
#include <new>

#include "esp_dsp.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#define SAMPLE_RATE             16000
#define AUDIO_FRAME_MS          1000
#define AUDIO_BUFFER_SAMPLES    (SAMPLE_RATE * AUDIO_FRAME_MS / 1000)

#define FFT_SIZE                512
#define FFT_STEP                256
#define NUM_FRAMES              ((AUDIO_BUFFER_SAMPLES - FFT_SIZE) / FFT_STEP + 1)
#define NUM_MEL_BINS            40
#define FEATURE_2D_SIZE         (NUM_FRAMES * NUM_MEL_BINS)

#define RMS_TARGET              0.03f
#define RMS_EPS                 1e-6f

#define SILENCE_RMS_THRESHOLD   0.003f
#define MAX_NORM_GAIN           8.0f

#define CONFIDENCE_THRESHOLD    0.50f
#define CONFIRM_COUNT           2
#define NOISE_TOLERANCE         5

#define TENSOR_ARENA_SIZE       (64 * 1024)

#define CLASS_NOISE             0
#define CLASS_KNOCK             1
#define CLASS_DOORBELL          2

static TaskHandle_t s_audio_task_handle = NULL;
static SemaphoreHandle_t s_result_mutex = NULL;
static volatile bool s_audio_task_stop = false;

static audio_result_t s_latest_result = {};

static tflite::MicroMutableOpResolver<20> *s_resolver = NULL;
static tflite::MicroInterpreter *s_interpreter = NULL;
static const tflite::Model *s_model = NULL;

static uint8_t s_tensor_arena[TENSOR_ARENA_SIZE] = {0};
static uint8_t s_resolver_buf[sizeof(tflite::MicroMutableOpResolver<20>)] = {0};
static uint8_t s_interpreter_buf[sizeof(tflite::MicroInterpreter)] = {0};

static int16_t s_audio_buffer[AUDIO_BUFFER_SAMPLES] = {0};
static float   s_fft_buffer[FFT_SIZE * 2] = {0};
static float   s_mel_filterbank[NUM_MEL_BINS][FFT_SIZE / 2 + 1] = {0};
static float   s_feature_buffer[FEATURE_2D_SIZE] = {0};

static int      s_last_class = -1;
static int      s_confirm_count = 0;
static int      s_noise_streak = 0;
static int      s_uncertain_streak = 0;
static int      s_inference_count = 0;
static bool     s_is_silence = false;

static inline float hz_to_mel(float f)
{
    return 2595.0f * log10f(1.0f + f / 700.0f);
}

static inline float mel_to_hz(float m)
{
    return 700.0f * (powf(10.0f, m / 2595.0f) - 1.0f);
}

static void init_mel_filterbank(void)
{
    float mel_low = hz_to_mel(0.0f);
    float mel_high = hz_to_mel((float)SAMPLE_RATE / 2.0f);
    float mel_points[NUM_MEL_BINS + 2];
    float freq_points[NUM_MEL_BINS + 2];

    for (int i = 0; i < NUM_MEL_BINS + 2; i++) {
        mel_points[i] = mel_low + i * (mel_high - mel_low) / (NUM_MEL_BINS + 1);
        freq_points[i] = mel_to_hz(mel_points[i]);
    }

    int num_freq_bins = FFT_SIZE / 2 + 1;
    float bin_freq_step = (float)SAMPLE_RATE / (float)FFT_SIZE;

    for (int m = 1; m <= NUM_MEL_BINS; m++) {
        for (int k = 0; k < num_freq_bins; k++) {
            float freq = k * bin_freq_step;
            float weight = 0.0f;
            if (freq >= freq_points[m - 1] && freq <= freq_points[m]) {
                weight = (freq - freq_points[m - 1]) / (freq_points[m] - freq_points[m - 1]);
            } else if (freq >= freq_points[m] && freq <= freq_points[m + 1]) {
                weight = (freq_points[m + 1] - freq) / (freq_points[m + 1] - freq_points[m]);
            }
            s_mel_filterbank[m - 1][k] = weight;
        }
    }
}

static void extract_features(void)
{
    memset(s_feature_buffer, 0, sizeof(s_feature_buffer));
    int num_freq_bins = FFT_SIZE / 2 + 1;

    float sum_sq = 0.0f;
    for (int i = 0; i < AUDIO_BUFFER_SAMPLES; i++) {
        float s = (float)s_audio_buffer[i] / 32768.0f;
        sum_sq += s * s;
    }
    float rms = sqrtf(sum_sq / AUDIO_BUFFER_SAMPLES) + RMS_EPS;

    s_is_silence = (rms < SILENCE_RMS_THRESHOLD);

    float gain = RMS_TARGET / rms;
    if (gain > MAX_NORM_GAIN) gain = MAX_NORM_GAIN;

    for (int frame = 0; frame < NUM_FRAMES; frame++) {
        int offset = frame * FFT_STEP;

        for (int i = 0; i < FFT_SIZE; i++) {
            float w = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (FFT_SIZE - 1));
            float s = ((float)s_audio_buffer[offset + i] / 32768.0f) * gain;
            if (s > 1.0f) s = 1.0f;
            if (s < -1.0f) s = -1.0f;
            s_fft_buffer[2 * i] = s * w;
            s_fft_buffer[2 * i + 1] = 0.0f;
        }

        dsps_fft2r_fc32(s_fft_buffer, FFT_SIZE);
        dsps_bit_rev_fc32(s_fft_buffer, FFT_SIZE);
        for (int i = 0; i < FFT_SIZE * 2; i++) {
            s_fft_buffer[i] /= (float)FFT_SIZE;
        }

        float *frame_feat = &s_feature_buffer[frame * NUM_MEL_BINS];
        for (int m = 0; m < NUM_MEL_BINS; m++) {
            float mel_val = 0.0f;
            for (int k = 0; k < num_freq_bins; k++) {
                float real = s_fft_buffer[2 * k];
                float imag = s_fft_buffer[2 * k + 1];
                float power = real * real + imag * imag;
                mel_val += power * s_mel_filterbank[m][k];
            }
            frame_feat[m] = logf(mel_val + 1e-6f);
        }
    }
}

static esp_err_t run_inference(audio_result_t *out)
{
    if (!s_interpreter) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_is_silence) {
        out->valid = true;
        out->event = AUDIO_EVENT_NONE;
        out->confidence = 0.0f;
        out->raw_probs[0] = 1.0f;
        out->raw_probs[1] = 0.0f;
        out->raw_probs[2] = 0.0f;

        s_noise_streak++;
        s_uncertain_streak = 0;
        if (s_noise_streak > NOISE_TOLERANCE) {
            s_last_class = CLASS_NOISE;
            s_confirm_count = 0;
        }
        return ESP_OK;
    }

    TfLiteTensor *input = s_interpreter->input(0);
    if (input == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    int input_size = input->bytes / (input->type == kTfLiteFloat32 ? 4 : 1);
    if (input_size != FEATURE_2D_SIZE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (input->type == kTfLiteFloat32) {
        for (int i = 0; i < FEATURE_2D_SIZE; i++) {
            input->data.f[i] = s_feature_buffer[i];
        }
    } else if (input->type == kTfLiteInt8) {
        float scale = input->params.scale;
        int zero_point = input->params.zero_point;
        for (int i = 0; i < FEATURE_2D_SIZE; i++) {
            int32_t val = (int32_t)(s_feature_buffer[i] / scale) + zero_point;
            if (val < -128) val = -128;
            if (val > 127) val = 127;
            input->data.int8[i] = (int8_t)val;
        }
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    TfLiteStatus invoke_status = s_interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        return ESP_FAIL;
    }

    TfLiteTensor *output = s_interpreter->output(0);
    if (output == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    float probs[3] = {0.0f};
    if (output->type == kTfLiteFloat32) {
        for (int i = 0; i < 3; i++) {
            probs[i] = output->data.f[i];
        }
    } else if (output->type == kTfLiteInt8) {
        float scale = output->params.scale;
        int zero_point = output->params.zero_point;
        for (int i = 0; i < 3; i++) {
            probs[i] = (output->data.int8[i] - zero_point) * scale;
        }
    } else {
        return ESP_ERR_NOT_SUPPORTED;
    }

    int max_idx = 0;
    for (int i = 1; i < 3; i++) {
        if (probs[i] > probs[max_idx]) max_idx = i;
    }

    out->valid = true;
    out->confidence = probs[max_idx];
    memcpy(out->raw_probs, probs, sizeof(probs));

    if (max_idx == CLASS_NOISE) {
        s_noise_streak++;
        if (s_noise_streak > NOISE_TOLERANCE) {
            s_last_class = CLASS_NOISE;
            s_confirm_count = 0;
        }
    } else if (out->confidence < CONFIDENCE_THRESHOLD) {
        s_uncertain_streak++;
        if (s_uncertain_streak > (NOISE_TOLERANCE * 2)) {
            s_last_class = -1;
            s_confirm_count = 0;
            s_uncertain_streak = 0;
        }
    } else {
        s_noise_streak = 0;
        s_uncertain_streak = 0;
        if (max_idx == s_last_class) {
            s_confirm_count++;
        } else {
            s_last_class = max_idx;
            s_confirm_count = 1;
        }
    }

    if (max_idx == CLASS_NOISE) {
        out->event = AUDIO_EVENT_NONE;
    } else if (out->confidence < CONFIDENCE_THRESHOLD) {
        out->event = AUDIO_EVENT_NONE;
    } else if (s_confirm_count < CONFIRM_COUNT) {
        out->event = AUDIO_EVENT_NONE;
    } else if (max_idx == CLASS_KNOCK) {
        out->event = AUDIO_EVENT_KNOCK;
    } else if (max_idx == CLASS_DOORBELL) {
        out->event = AUDIO_EVENT_DOORBELL;
    } else {
        out->event = AUDIO_EVENT_UNKNOWN;
    }

    return ESP_OK;
}

static void audio_task(void *pvParameters)
{
    int32_t read_idx = 0;
    int16_t temp_buf[256];
    int debug_count = 0;

    s_audio_task_stop = false;

    while (!s_audio_task_stop) {
        int samples_read = mic_read_samples(temp_buf, 256, pdMS_TO_TICKS(100));

        if (samples_read > 0) {
            debug_count++;
            if (debug_count % 64 == 0) {
                int32_t sum = 0;
                for (int i = 0; i < samples_read; i++) {
                    sum += temp_buf[i] * temp_buf[i];
                }
                float rms = sqrtf((float)sum / (float)samples_read) / 32768.0f;
                ESP_LOGI("AUDIO_AI", "麦克风采样: %d samples, RMS=%.4f", samples_read, rms);
            }

            for (int i = 0; i < samples_read; i++) {
                s_audio_buffer[read_idx] = temp_buf[i];
                read_idx++;
                if (read_idx >= AUDIO_BUFFER_SAMPLES) {
                    read_idx = 0;

                    audio_result_t result = {};
                    s_inference_count++;
                    extract_features();
                    if (run_inference(&result) == ESP_OK) {
                        result.inference_seq = s_inference_count;
                        xSemaphoreTake(s_result_mutex, portMAX_DELAY);
                        memcpy(&s_latest_result, &result, sizeof(audio_result_t));
                        xSemaphoreGive(s_result_mutex);

                        if (debug_count % 64 == 0) {
                            const char *ev_name = (result.event == AUDIO_EVENT_KNOCK) ? "敲门" :
                                                 (result.event == AUDIO_EVENT_DOORBELL) ? "门铃" :
                                                 (result.event == AUDIO_EVENT_NONE) ? "无" : "未知";
                            ESP_LOGI("AUDIO_AI", "推理结果: %s, 置信度=%.2f, seq=%d",
                                     ev_name, result.confidence, result.inference_seq);
                        }
                    }
                }
            }
        } else {
            ESP_LOGW("AUDIO_AI", "麦克风读取失败: %d", samples_read);
        }
    }

    s_audio_task_handle = NULL;
    ESP_LOGI("AUDIO_AI", "audio_task 已优雅退出");
    vTaskDelete(NULL);
}

esp_err_t audio_ai_init(void)
{
    if (s_result_mutex == NULL) {
        s_result_mutex = xSemaphoreCreateMutex();
    }

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, FFT_SIZE);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        return ret;
    }

    init_mel_filterbank();

    ret = mic_init();
    if (ret != ESP_OK) {
        return ret;
    }

    s_model = tflite::GetModel(g_model_data);
    if (s_model->version() != TFLITE_SCHEMA_VERSION) {
        return ESP_ERR_INVALID_VERSION;
    }

    s_resolver = reinterpret_cast<tflite::MicroMutableOpResolver<20>*>(s_resolver_buf);
    new(s_resolver) tflite::MicroMutableOpResolver<20>();
    s_resolver->AddConv2D();
    s_resolver->AddMaxPool2D();
    s_resolver->AddAveragePool2D();
    s_resolver->AddFullyConnected();
    s_resolver->AddSoftmax();
    s_resolver->AddRelu();
    s_resolver->AddLogistic();
    s_resolver->AddMul();
    s_resolver->AddAdd();
    s_resolver->AddSub();
    s_resolver->AddReshape();
    s_resolver->AddShape();
    s_resolver->AddSqueeze();
    s_resolver->AddStridedSlice();
    s_resolver->AddMean();
    s_resolver->AddQuantize();
    s_resolver->AddDequantize();
    s_resolver->AddPad();
    s_resolver->AddPack();
    s_resolver->AddConcatenation();

    s_interpreter = reinterpret_cast<tflite::MicroInterpreter*>(s_interpreter_buf);
    new(s_interpreter) tflite::MicroInterpreter(s_model, *s_resolver, s_tensor_arena, TENSOR_ARENA_SIZE);

    TfLiteStatus alloc_status = s_interpreter->AllocateTensors();
    if (alloc_status != kTfLiteOk) {
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t audio_ai_start(void)
{
    if (!s_interpreter) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_audio_task_handle == NULL) {
        BaseType_t xRet = xTaskCreatePinnedToCore(
            audio_task, "audio_task", 8192, NULL, 5, &s_audio_task_handle, 1);
        if (xRet != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }

    xSemaphoreTake(s_result_mutex, portMAX_DELAY);
    memset(&s_latest_result, 0, sizeof(audio_result_t));
    s_inference_count = 0;
    s_last_class = -1;
    s_confirm_count = 0;
    s_noise_streak = 0;
    s_uncertain_streak = 0;
    xSemaphoreGive(s_result_mutex);

    ESP_LOGI("AUDIO_AI", "音频推理状态已重置");

    return ESP_OK;
}

esp_err_t audio_ai_stop(void)
{
    if (s_audio_task_handle) {
        s_audio_task_stop = true;
        /* 等待任务自行退出（最多等2秒） */
        int wait_count = 0;
        while (s_audio_task_handle != NULL && wait_count < 20) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        if (s_audio_task_handle != NULL) {
            ESP_LOGW("AUDIO_AI", "audio_task 未优雅退出，强制删除");
            vTaskDelete(s_audio_task_handle);
            s_audio_task_handle = NULL;
        }
    }
    return ESP_OK;
}

esp_err_t audio_ai_get_latest_result(audio_result_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;

    xSemaphoreTake(s_result_mutex, portMAX_DELAY);
    memcpy(result, &s_latest_result, sizeof(audio_result_t));
    xSemaphoreGive(s_result_mutex);
    return ESP_OK;
}

esp_err_t audio_ai_run_once(audio_result_t *result)
{
    if (!s_interpreter) return ESP_ERR_INVALID_STATE;

    int total_read = 0;
    while (total_read < AUDIO_BUFFER_SAMPLES) {
        int n = mic_read_samples(&s_audio_buffer[total_read], AUDIO_BUFFER_SAMPLES - total_read, pdMS_TO_TICKS(100));
        if (n <= 0) break;
        total_read += n;
    }

    extract_features();
    esp_err_t ret = run_inference(result);
    result->inference_seq = s_inference_count;

    xSemaphoreTake(s_result_mutex, portMAX_DELAY);
    memcpy(&s_latest_result, result, sizeof(audio_result_t));
    xSemaphoreGive(s_result_mutex);

    return ret;
}
