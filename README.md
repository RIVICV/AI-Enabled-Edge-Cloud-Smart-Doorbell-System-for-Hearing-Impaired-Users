
# 🌱 听障人士智能门铃系统
# 🌱 Smart Doorbell System for Hearing-Impaired Users

> **English** | [**中文**](#中文版)
>
> **Smart Doorbell System for Hearing-Impaired Users**  
> Based on ESP32-S3 · Multi-modal Sensor Fusion · Edge AI · Cloud LLM Interaction · WeChat Mini Program Remote Control
>
> **基于 ESP32-S3 的多模态感知融合 · 边缘 AI · 云端大模型交互 · 微信小程序远程控制**

---

# English Version

## 📌 Project Overview

This project addresses the core pain point of hearing-impaired individuals being unable to perceive traditional doorbell auditory cues. We have designed a **low-cost, highly reliable, offline-first** accessible smart doorbell system. Built around the Espressif ESP32-S3 platform, the system leverages its full-stack capabilities including **WiFi + BLE + NPU** to construct a **triple-perception fusion decision mechanism** consisting of "physical button trigger + PIR human presence detection + MEMS microphone audio feature matching." Through local edge AI inference, it performs lightweight audio classification for doorbell rings and knocks, while integrating cloud-based large language models for natural language interaction and intelligent visitor record querying.

**Core Advantages:**
- ✅ **Offline-First**: Core doorbell functions operate autonomously even when WiFi is disconnected
- ✅ **Triple Decision**: Effectively filters false alarms from accidental touches and environmental noise
- ✅ **No Camera**: OLED "message + trace" design balances functionality with user privacy
- ✅ **Multi-modal Alerts**: Visual flashing + text display + tactile vibration covers all scenarios
- ✅ **Cloud AI**: Volcano Engine LLM enables natural language Q&A

---

## 🏗️ System Architecture

The system adopts an **end-edge-cloud** full-link four-layer IoT architecture:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     Application Layer (WeChat Mini Program)                 │
│           Visit Query · AI Q&A · History Records · Email Config             │
├─────────────────────────────────────────────────────────────────────────────┤
│                    Cloud Application Layer (Tencent Cloud + Volcano Engine) │
│           Flask Backend · MQTT Broker · SQLite DB · LLM API                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                       Control Layer (ESP32-S3 Local)                        │
│          Triple Fusion · Edge AI Inference (NPU) · Multi-modal Control      │
├─────────────────────────────────────────────────────────────────────────────┤
│                       Perception Layer (ESP32-S3 + Sensors)                 │
│      Button · PIR · MEMS Mic · LED Strip · OLED · Vibration Motor           │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Data Flow

```
Visitor Presses Bell → Triple Decision → Valid Visit → Local Alert + MQTT Upload → Cloud Storage → Mini Program Query → AI Response
```

---

## 📁 Project Structure

```
smart_doorbell_weixin/
├── app.py                          # Flask Backend Main Program
├── config.example.py               # Config Template (copy to config.py)
├── requirements.txt                # Python Dependencies
├── README.md                       # Project Documentation
│
├── miniapp/                        # WeChat Mini Program Frontend
│   ├── pages/
│   │   ├── index/                  # Home (AI Q&A + Recent Visits)
│   │   ├── history/                # History Records
│   │   ├── setting/                # Settings
│   │   └── email/                  # Email Configuration
│   ├── app.json
│   ├── app.js
│   └── app.wxss
│
└── hardware/                       # ESP32-S3 Hardware Code
    ├── client/                     # Outdoor Main Board
    │   ├── main/
    │   │   ├── main.c              # Main Program
    │   │   ├── wifi.c / wifi.h     # WiFi Connection
    │   │   ├── mqtt.c / mqtt.h     # MQTT Communication
    │   │   ├── audio_ai.cpp        # Edge AI Audio Inference
    │   │   ├── event_fusion.c      # Triple-channel Fusion
    │   │   ├── esp_now_outdoor.c   # ESP-NOW Communication
    │   │   ├── light.c             # RGB LED Control
    │   │   ├── oled.c              # OLED Display
    │   │   ├── pir.c               # PIR Sensor
    │   │   └── buzzer.c            # Buzzer
    │   └── CMakeLists.txt
    │
    └── server/                     # Indoor Sub-board
        ├── main/
        │   ├── main.c              # Main Program
        │   ├── esp_now_indoor.c    # ESP-NOW Communication
        │   ├── vibrator.c          # Vibration Motor Control
        │   └── emergency_button.c  # Emergency Button
        └── CMakeLists.txt
```

---

## 🛠️ Technology Stack

| Layer | Technology | Description |
|-------|------------|-------------|
| **Hardware MCU** | ESP32-S3-WROOM-1 | WiFi + BLE + NPU integrated, Dual-core Xtensa LX7 |
| **Framework** | ESP-IDF v5.x | Espressif Official IoT Framework |
| **Edge AI** | TensorFlow Lite Micro + ESP-DSP | 40-dim log-Mel + 3-class model (knock/doorbell/noise) |
| **Protocol** | MQTT v3.1.1 + ESP-NOW | WAN Cloud + Short-range Local Communication |
| **Cloud Backend** | Python Flask + SQLite | Lightweight Web Server + Embedded Database |
| **AI Service** | Volcano Engine Doubao LLM | Smart Q&A & Natural Language Interaction |
| **Frontend** | WeChat Mini Program Native | WXML + WXSS + JavaScript |
| **Email** | QQ Mail SMTP | Emergency Call Email Notification |

---

## 🔧 Features

### Hardware

| Feature | Description |
|---------|-------------|
| **Triple Fusion Decision** | Button + PIR + Audio AI, ≥2 channels trigger for valid visits |
| **Edge AI Audio Recognition** | Local inference on ESP32-S3 NPU for knock/doorbell/noise |
| **Offline-First Operation** | Core functions remain autonomous when WiFi is down |
| **OLED Message + Trace** | Visitor can cycle preset messages via long press |
| **Multi-modal Alerts** | RGB flash + OLED text + Buzzer + Vibration motor |
| **ESP-NOW Dual-board Comm** | Low-latency short-range wireless between boards |
| **One-tap Emergency** | Indoor emergency button triggers email notification |

### Cloud & Interaction

| Feature | Description |
|---------|-------------|
| **MQTT Relay** | Self-hosted Mosquitto Broker, QoS=1 ensures message delivery |
| **History Storage** | SQLite persistence, time-based query support |
| **AI Smart Q&A** | Volcano Engine LLM analyzes logs, generates natural responses |
| **Mini Program Interaction** | Home AI Q&A + History Query + Dynamic Email Config |
| **Emergency Email** | Auto-sends email to preset contact (configurable in mini program) |
| **Visit Statistics** | Total, Today, This Week auto-calculated |

---

## 🚀 Quick Start

### 1. Clone Repository

```bash
git clone https://github.com/RIVICV/AI-Enabled-Edge-Cloud-Smart-Doorbell-System-for-Hearing-Impaired-Users.git
cd smart_doorbell_weixin
```

### 2. Deploy Backend

```bash
# Install Python dependencies
pip install -r requirements.txt

# Create config file
cp config.example.py config.py
# Edit config.py with your Volcano Engine API Key, Endpoint ID, QQ Mail SMTP

# Start service
python3 app.py
```

### 3. Build Hardware

```bash
# Build outdoor board
cd hardware/client
idf.py set-target esp32s3
idf.py build
idf.py flash

# Build indoor board
cd ../server
idf.py set-target esp32s3
idf.py build
idf.py flash
```

### 4. Run Mini Program

Open the `miniapp/` folder with WeChat Developer Tools, configure the API address to your server IP.

---

## 📊 Performance Metrics

| Metric | Value |
|--------|-------|
| Response Latency | ≤ 200ms (end-to-end) |
| Audio Recognition Accuracy | ≥ 90% (knock/doorbell) |
| False Alarm Rate | ≤ 5% (daily home environment) |
| Offline Availability | 100% (core functions cloud-independent) |
| MQTT Message Delivery | ≥ 99% (QoS=1) |
| Wearable Vibration Battery | ≥ 7 days (500mAh battery) |

---



# 中文版

## 📌 项目简介

本项目针对听障群体无法感知传统门铃听觉提示的核心痛点，设计了一套**低成本、高可靠、离线优先**的无障碍智能门铃系统。系统以乐鑫 ESP32-S3 平台为核心硬件，充分利用其内置 **WiFi + BLE + NPU** 全栈能力，构建了“物理按键触发 + PIR 人体存在检测 + MEMS 麦克风音频特征匹配”的**三重感知融合判决机制**，通过本地边缘 AI 推理完成敲门声/门铃声的轻量化音频分类，并结合云端大模型实现自然语言交互与来访记录智能查询。

**核心优势：**
- ✅ **离线优先**：WiFi 断开时核心门铃功能完全自治运行
- ✅ **三重判决**：从源头过滤误触、环境噪声等无效干扰
- ✅ **无摄像头**：OLED“留言+留痕”功能，兼顾功能完整性与用户隐私
- ✅ **多模态提醒**：视觉频闪 + 文字提示 + 贴身振动，覆盖全场景
- ✅ **云端 AI**：火山引擎大模型赋能，支持自然语言问答

---

## 🏗️ 系统架构

系统采用**端-边-云**全链路四层物联网架构：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           应用层（微信小程序）                                │
│              来访查询 · AI 问答 · 历史记录 · 邮箱配置                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                           云应用层（腾讯云 + 火山引擎）                       │
│          Flask 后端 · MQTT Broker · SQLite 数据库 · 大模型 API               │
├─────────────────────────────────────────────────────────────────────────────┤
│                           控制层（ESP32-S3 本地）                            │
│         三重感知融合 · 边缘 AI 推理（NPU）· 多模态提醒控制                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                           感知层（ESP32-S3 + 传感器）                        │
│       物理按键 · PIR 人体红外 · MEMS 麦克风 · 灯带 · OLED · 振动电机           │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 数据流

```
访客按铃 → 三重判决 → 有效来访 → 本地多模态提醒 + MQTT 上报 → 云端存储 → 小程序查询 → AI 回答
```

---

## 📁 项目结构

```
smart_doorbell_weixin/
├── app.py                          # Flask 后端主程序
├── config.example.py               # 配置文件模板（复制为 config.py 使用）
├── requirements.txt                # Python 依赖清单
├── README.md                       # 项目文档
│
├── miniapp/                        # 微信小程序前端
│   ├── pages/
│   │   ├── index/                  # 首页（AI 问答 + 最近来访）
│   │   ├── history/                # 历史记录页
│   │   ├── setting/                # 设置页
│   │   └── email/                  # 邮箱配置页
│   ├── app.json
│   ├── app.js
│   └── app.wxss
│
└── hardware/                       # ESP32-S3 硬件代码
    ├── client/                     # 室外主板
    │   ├── main/
    │   │   ├── main.c              # 主程序
    │   │   ├── wifi.c / wifi.h     # WiFi 连接
    │   │   ├── mqtt.c / mqtt.h     # MQTT 通信
    │   │   ├── audio_ai.cpp        # 边缘 AI 音频推理
    │   │   ├── event_fusion.c      # 三通道融合判决
    │   │   ├── esp_now_outdoor.c   # ESP-NOW 通信
    │   │   ├── light.c             # RGB 灯带控制
    │   │   ├── oled.c              # OLED 显示
    │   │   ├── pir.c               # PIR 传感器
    │   │   └── buzzer.c            # 蜂鸣器
    │   └── CMakeLists.txt
    │
    └── server/                     # 室内从板
        ├── main/
        │   ├── main.c              # 主程序
        │   ├── esp_now_indoor.c    # ESP-NOW 通信
        │   ├── vibrator.c          # 振动马达控制
        │   └── emergency_button.c  # 紧急按键
        └── CMakeLists.txt
```

---

## 🛠️ 技术栈

| 层级 | 技术选型 | 说明 |
|------|----------|------|
| **硬件主控** | ESP32-S3-WROOM-1 | 集成 WiFi + BLE + NPU，双核 Xtensa LX7 |
| **开发框架** | ESP-IDF v5.x | 乐鑫官方物联网开发框架 |
| **边缘 AI** | TensorFlow Lite Micro + ESP-DSP | 40 维 log-Mel 特征 + 三分类模型（敲门/门铃/噪声） |
| **通信协议** | MQTT v3.1.1 + ESP-NOW | 广域上云 + 本地短距通信 |
| **云端框架** | Python Flask + SQLite | 轻量级 Web 后端 + 嵌入式数据库 |
| **AI 服务** | 火山引擎豆包大模型 | 智能问答与自然语言交互 |
| **前端框架** | 微信小程序原生 | WXML + WXSS + JavaScript |
| **邮件服务** | QQ 邮箱 SMTP | 紧急呼叫邮件通知 |

---

## 🔧 功能特性

### 硬件端

| 功能 | 说明 |
|------|------|
| **三重感知融合判决** | 物理按键 + PIR 红外 + 音频 AI，≥2 通道触发才判定有效来访 |
| **边缘 AI 音频识别** | ESP32-S3 NPU 本地推理，识别敲门声/门铃声/环境噪声 |
| **离线优先运行** | WiFi 断开时核心门铃功能完全自治，不影响使用 |
| **OLED 留言+留痕** | 访客可循环切换预设留言（快递/外卖/物业/维修等） |
| **多模态提醒** | RGB 灯带频闪 + OLED 文字显示 + 蜂鸣器 + 振动马达 |
| **ESP-NOW 双板通信** | 室外板与室内板低功耗短距无线通信 |
| **一键紧急呼叫** | 室内板紧急按钮触发邮件通知 |

### 云端与交互

| 功能 | 说明 |
|------|------|
| **MQTT 消息中转** | 自建 Mosquitto Broker，QoS=1 保障消息可靠到达 |
| **历史记录存储** | SQLite 持久化存储，支持按时间回溯 |
| **AI 智能问答** | 火山引擎大模型分析日志，生成自然语言回答 |
| **微信小程序交互** | 首页 AI 问答 + 历史记录查询 + 邮箱动态配置 |
| **紧急呼叫邮件通知** | 触发后自动发送邮件到预设联系人（邮箱可随时修改） |
| **来访统计** | 总来访、今日、本周自动统计 |

---

## 🚀 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/RIVICV/AI-Enabled-Edge-Cloud-Smart-Doorbell-System-for-Hearing-Impaired-Users.git
cd smart_doorbell_weixin
```

### 2. 部署后端

```bash
# 安装 Python 依赖
pip install -r requirements.txt

# 创建配置文件
cp config.example.py config.py
# 编辑 config.py，填入火山引擎 API Key、Endpoint ID、QQ 邮箱授权码

# 启动服务
python3 app.py
```

### 3. 编译硬件

```bash
# 编译室外板
cd hardware/client
idf.py set-target esp32s3
idf.py build
idf.py flash

# 编译室内板
cd ../server
idf.py set-target esp32s3
idf.py build
idf.py flash
```

### 4. 运行小程序

使用微信开发者工具打开 `miniapp/` 文件夹，配置 API 地址为你的服务器 IP 即可。

---

## 📊 性能指标

| 指标 | 数值 |
|------|------|
| 响应时延 | ≤ 200ms（端到端提醒） |
| 音频识别准确率 | ≥ 90%（敲门/门铃） |
| 误报率 | ≤ 5%（日常居家环境） |
| 离线可用性 | 100%（核心功能不依赖云端） |
| MQTT 消息到达率 | ≥ 99%（QoS=1） |
| 随身振动器续航 | ≥ 7 天（500mAh 电池） |

---

## 👥 团队成员

| 成员 | 角色 | 主要任务 | 具体职责 |
|------|------|----------|----------|
| **崔琇淇** | 云端与交互负责人 | 云平台配置、后端API开发、小程序开发、AI对接 | 腾讯云轻量服务器部署、Flask后端开发、MQTT服务搭建、SQLite数据库设计、火山引擎大模型API对接、微信小程序全栈开发、紧急呼叫邮件通知实现 |
| **陈思** | 硬件通信与系统集成负责人 | ESP-NOW双板通信、MQTT上传、硬件整体架构 | 室外板client与室内板server的ESP-NOW无线通信协议实现、WiFi+MQTT云端上传、判决信息数据规范化、硬件整体代码整合、离线MQTT队列缓存机制 |
| **黄佳翠** | 嵌入式驱动与集成测试负责人 | 单模块驱动开发、外围功能、集成测试 | WS2812灯带RMT驱动、INMP441麦克风I2S驱动、PIR人体红外GPIO驱动、振动马达PWM驱动、SSD1306 OLED SPI驱动、无源蜂鸣器模拟门铃声音、按键GPIO中断检测、面包板搭建与整体功能模块测试 |
| **龚梁浩** | 边缘AI与融合判决负责人 | 音频AI识别、门外语音转文字关键字识别、三通道融合判决 | ESP-DSP FFT音频特征提取、ESP-SR语音识别、TFLite Micro三分类模型推理、按键+PIR+音频三通道融合判决算法、主程序框架设计、音频模型训练与量化部署 |

---

## 🙏 致谢

本项目的顺利完成离不开以下方面的支持与帮助：

**团队成员**：
- 感谢**陈思**同学在硬件通信与系统集成方面的出色工作，完成了ESP-NOW双板通信协议的实现、WiFi+MQTT云端上传以及离线MQTT队列缓存机制的设计，确保了数据在双板之间及云端之间的可靠传输。
- 感谢**黄佳翠**同学在嵌入式驱动开发与硬件集成方面的扎实贡献，完成了WS2812灯带、INMP441麦克风、PIR传感器、振动马达、OLED屏幕、蜂鸣器等八大硬件模块的驱动编写与调试，并完成了面包板整体搭建与功能测试。
- 感谢**龚梁浩**同学在边缘AI与融合判决方面的深入工作，完成了ESP-DSP FFT音频特征提取、TFLite Micro三分类模型推理、三通道融合判决算法以及音频模型的训练与量化部署，为系统提供了本地智能识别能力。
在项目开发中，团队四人各司其职、通力协作，共同面对技术挑战、分享解决方案，从硬件搭建到云端部署、从驱动调试到AI推理，每个环节都凝聚了团队的智慧与汗水。

**乐鑫科技**：感谢乐鑫提供的ESP32-S3芯片与ESP-IDF v5.x开发框架，其完善的文档与丰富的示例代码为项目硬件开发提供了坚实的技术支撑。

**火山引擎**：感谢火山引擎提供的豆包大模型API服务，让系统具备了智能问答与自然语言交互能力。

**腾讯云**：感谢腾讯云提供的轻量应用服务器免费试用机会，使项目能够以零成本完成云端部署与验证。

---
