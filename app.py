# =======================================================
# 模块导入
# 导入项目所需的所有Python标准库和第三方库
# =======================================================
import os
import json
import sqlite3
import smtplib
import re
from email.mime.text import MIMEText
from email.header import Header
from datetime import datetime
from flask import Flask, request, jsonify
from paho.mqtt import client as mqtt_client
import requests

# ==================== 配置区 ====================
# 所有敏感信息从环境变量读取，不硬编码在代码中
# 需要在服务器上设置以下环境变量：
#   export VOLC_API_KEY="your_api_key"
#   export VOLC_ENDPOINT="your_endpoint_id"
#   export SMTP_PASSWORD="your_smtp_password"
# ===================================================

# ----- MQTT 配置（非敏感，可直接写在代码中） -----
MQTT_BROKER = '114.132.168.16'
MQTT_PORT = 1883
TOPIC_EVENT = "device/esp32_s3_01/event"
TOPIC_EMERGENCY = "device/esp32_s3_01/emergency"

# ----- 火山引擎大模型配置（从环境变量读取） -----
VOLC_API_KEY = os.environ.get('VOLC_API_KEY', '你的火山引擎API_KEY')
VOLC_ENDPOINT = os.environ.get('VOLC_ENDPOINT', '你的模型部署Endpoint_ID')

# ----- QQ邮箱SMTP配置（从环境变量读取） -----
SMTP_HOST = "smtp.qq.com"
SMTP_PORT = 465
FROM_EMAIL = "have6666@qq.com"
SMTP_PASSWORD = os.environ.get('SMTP_PASSWORD', '你的授权码')
# =============================================

app = Flask(__name__)

# =============================================
# 数据库初始化
# =============================================

def init_db():
    """初始化数据库，创建事件表和配置表"""
    conn = sqlite3.connect('smarthome.db')
    cursor = conn.cursor()
    
    # 事件日志表
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS event_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            device_id TEXT,
            event_type TEXT,
            msg_content TEXT,
            trigger_time TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # 配置表（存储邮箱等配置）
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS config (
            key TEXT PRIMARY KEY,
            value TEXT
        )
    ''')
    
    conn.commit()
    conn.close()
    print("【系统通知】SQLite 数据库初始化成功！")


def get_email_config():
    """获取紧急联系人邮箱"""
    try:
        conn = sqlite3.connect('smarthome.db')
        cursor = conn.cursor()
        cursor.execute("SELECT value FROM config WHERE key = 'emergency_email'")
        row = cursor.fetchone()
        conn.close()
        return row[0] if row else None
    except Exception as e:
        print(f"获取邮箱配置失败: {e}")
        return None


def set_email_config(email):
    """设置紧急联系人邮箱"""
    try:
        conn = sqlite3.connect('smarthome.db')
        cursor = conn.cursor()
        cursor.execute(
            "INSERT OR REPLACE INTO config (key, value) VALUES ('emergency_email', ?)",
            (email,)
        )
        conn.commit()
        conn.close()
        return True
    except Exception as e:
        print(f"保存邮箱配置失败: {e}")
        return False


# =============================================
# 发送邮件函数
# =============================================

def send_emergency_email():
    """发送紧急呼叫邮件通知到子女邮箱"""
    try:
        # 从数据库读取收件人邮箱
        to_email = get_email_config()
        if not to_email:
            print("❌ 未设置紧急联系人邮箱，请在小程序中配置")
            return False
        
        subject = "🚨 紧急警报！智能门铃紧急呼叫"
        content = f"""
        ⚠️⚠️⚠️ 紧急通知 ⚠️⚠️⚠️

        智能门铃系统检测到紧急呼叫按钮已被按下！

        时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}
        设备：ESP32-S3 智能门铃

        请立即联系确认情况！

        ---
        此邮件由智能门铃系统自动发送，请勿回复。
        """
        
        msg = MIMEText(content, 'plain', 'utf-8')
        msg['From'] = FROM_EMAIL
        msg['To'] = to_email
        msg['Subject'] = subject
        
        with smtplib.SMTP_SSL(SMTP_HOST, SMTP_PORT) as server:
            server.login(FROM_EMAIL, SMTP_PASSWORD)
            server.sendmail(FROM_EMAIL, [to_email], msg.as_string())
        
        print(f"✅ 紧急邮件已发送成功！收件人: {to_email}")
        return True
    except Exception as e:
        print(f"❌ 邮件发送失败: {e}")
        return False


def send_test_email(to_email):
    """发送测试邮件"""
    try:
        subject = "🔔 智能门铃 - 测试邮件"
        content = f"""
        这是一封来自智能门铃系统的测试邮件。

        收件人：{to_email}
        时间：{datetime.now().strftime('%Y-%m-%d %H:%M:%S')}

        如果您收到此邮件，说明邮箱设置正确！

        ---
        此邮件由智能门铃系统自动发送。
        """
        msg = MIMEText(content, 'plain', 'utf-8')
        msg['From'] = FROM_EMAIL
        msg['To'] = to_email
        msg['Subject'] = subject
        
        with smtplib.SMTP_SSL(SMTP_HOST, SMTP_PORT) as server:
            server.login(FROM_EMAIL, SMTP_PASSWORD)
            server.sendmail(FROM_EMAIL, [to_email], msg.as_string())
        
        print(f"✅ 测试邮件已发送！收件人: {to_email}")
        return True
    except Exception as e:
        print(f"❌ 测试邮件发送失败: {e}")
        return False


# =============================================
# MQTT 回调函数
# =============================================

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        print("【系统通知】成功连接到 MQTT 服务器！正在监听门铃信号...")
        client.subscribe(TOPIC_EVENT)
        client.subscribe(TOPIC_EMERGENCY)
        print(f"【调试】已订阅 Topic: {TOPIC_EVENT}")
        print(f"【调试】已订阅 Topic: {TOPIC_EMERGENCY}")
    else:
        print(f"【错误】MQTT 连接失败，错误码: {rc}")


def on_message(client, userdata, msg):
    print("【调试】on_message 被触发了！！！")
    payload = msg.payload.decode()
    print(f"【收到消息】主题: {msg.topic} -> 内容: {payload}")
    
    try:
        data = json.loads(payload)
        event_type = data.get('event', 'unknown')
        
        # 获取当前北京时间
        beijing_time = datetime.now().strftime('%Y-%m-%d %H:%M:%S')
        
        # ========== 紧急呼叫处理 ==========
        if event_type == "emergency" or msg.topic == TOPIC_EMERGENCY:
            print("🚨 紧急呼叫！开始发送邮件通知...")
            send_emergency_email()
            
            conn = sqlite3.connect('smarthome.db')
            cursor = conn.cursor()
            cursor.execute(
                "INSERT INTO event_logs (device_id, event_type, msg_content, trigger_time) VALUES (?, ?, ?, ?)",
                ("esp32_s3_01", "emergency", "⚠️ 紧急呼叫！已发送邮件", beijing_time)
            )
            conn.commit()
            conn.close()
            print("【数据库】已记录紧急事件")
            return
        
        # ========== 正常门铃事件处理 ==========
        # cmd_id映射表（7种场景）
        cmd_id_map = {
            0: "有访客来访！",
            1: "您的外卖到了！",
            2: "您的快递到了！",
            3: "物业来访！",
            4: "上门维修到了！",
            5: "有人寻求帮助！",
            6: "门外有紧急事件！"
        }
        
        # 从 payload 中获取 cmd_id，如果没有则默认为 0
        cmd_id = data.get('cmd_id', 0)
        msg_content = cmd_id_map.get(cmd_id, "普通来访（无留言）")
        
        print(f"【cmd_id 映射】cmd_id={cmd_id} -> 内容: {msg_content}")
        
        conn = sqlite3.connect('smarthome.db')
        cursor = conn.cursor()
        cursor.execute(
            "INSERT INTO event_logs (device_id, event_type, msg_content, trigger_time) VALUES (?, ?, ?, ?)",
            ("esp32_s3_01", event_type, msg_content, beijing_time)
        )
        conn.commit()
        conn.close()
        print("【数据库】已成功记录本次来访事件。")
        
    except Exception as e:
        print(f"【错误】解析MQTT消息并入库时发生异常: {e}")


# =============================================
# Flask HTTP 接口
# =============================================

@app.route('/api/ask', methods=['POST'])
def ask_ai():
    user_data = request.json
    user_question = user_data.get('question', '最近有人来过吗？')
    print(f"【收到用户提问】: {user_question}")
    
    try:
        conn = sqlite3.connect('smarthome.db')
        cursor = conn.cursor()
        cursor.execute("SELECT trigger_time, event_type, msg_content FROM event_logs ORDER BY id DESC LIMIT 5")
        rows = cursor.fetchall()
        conn.close()
        
        if not rows:
            return jsonify({"answer": "家里最近安安静静，没有任何人来按门铃哦。"})
        
        log_context = "以下是智能门铃最近的来访记录：\n"
        for row in rows:
            log_context += f"- 时间: {row[0]}, 事件: {row[1]}, 访客留言: {row[2]}\n"
            
    except Exception as e:
        return jsonify({"answer": f"读取门铃日志失败，错误原因: {e}"})

    # ----- 测试模式判断 -----
    # 如果 API_KEY 包含"你的"，说明未配置真实密钥，使用模拟回答
    if "你的" in VOLC_API_KEY:
        mock_answer = f"【测试模式】AI 收到你的问题啦！\n我帮你看了日志，最近一次来访在 {rows[0][0]}，事件是 {rows[0][1]}，留言是：{rows[0][2]}。"
        return jsonify({"answer": mock_answer})

    try:
        url = "https://ark.cn-beijing.volces.com/api/v3/chat/completions"
        headers = {
            "Authorization": f"Bearer {VOLC_API_KEY}",
            "Content-Type": "application/json"
        }
        payload = {
            "model": VOLC_ENDPOINT,
            "messages": [
                {"role": "system", "content": "你是一个温柔贴心的智能家居管家。请根据提供的门铃日志，用亲切、精炼的语言回答听障人士家属的提问。"},
                {"role": "user", "content": f"{log_context}\n\n根据以上日志，请回答用户的问题：{user_question}"}
            ]
        }
        response = requests.post(url, headers=headers, json=payload, timeout=30)
        
        if response.status_code == 200:
            result = response.json()
            ai_reply = result["choices"][0]["message"]["content"]
            return jsonify({"answer": ai_reply})
        else:
            return jsonify({"answer": f"API 返回错误: {response.status_code}"})
            
    except Exception as e:
        return jsonify({"answer": f"呼叫火山大模型失败: {str(e)}"})


@app.route('/api/history', methods=['GET'])
def get_history():
    try:
        # 获取limit参数，默认为20
        limit = request.args.get('limit', 20, type=int)
        
        conn = sqlite3.connect('smarthome.db')
        cursor = conn.cursor()
        cursor.execute(
            "SELECT id, trigger_time, event_type, msg_content FROM event_logs ORDER BY id DESC LIMIT ?",
            (limit,)
        )
        rows = cursor.fetchall()
        conn.close()
        
        records = []
        for row in rows:
            records.append({
                'id': row[0],
                'trigger_time': row[1],
                'event_type': row[2],
                'msg_content': row[3] or '普通来访'
            })
        
        return jsonify({
            'code': 0,
            'data': records,
            'total': len(records)
        })
    except Exception as e:
        return jsonify({
            'code': -1,
            'message': str(e)
        }), 500


# =============================================
# 邮箱管理 API
# =============================================

@app.route('/api/email', methods=['GET'])
def get_email():
    """获取紧急联系人邮箱"""
    try:
        email = get_email_config()
        return jsonify({
            'code': 0,
            'email': email or ''
        })
    except Exception as e:
        return jsonify({
            'code': -1,
            'message': str(e)
        }), 500


@app.route('/api/email', methods=['POST'])
def set_email():
    """设置紧急联系人邮箱"""
    try:
        data = request.json
        email = data.get('email', '').strip()
        
        if not email:
            return jsonify({
                'code': -1,
                'message': '邮箱不能为空'
            }), 400
        
        # 验证邮箱格式
        if not re.match(r'^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$', email):
            return jsonify({
                'code': -1,
                'message': '邮箱格式不正确'
            }), 400
        
        set_email_config(email)
        return jsonify({
            'code': 0,
            'message': '设置成功'
        })
    except Exception as e:
        return jsonify({
            'code': -1,
            'message': str(e)
        }), 500


@app.route('/api/email/test', methods=['POST'])
def test_email():
    """发送测试邮件"""
    try:
        data = request.json
        email = data.get('email', '').strip()
        
        if not email:
            return jsonify({
                'code': -1,
                'message': '邮箱不能为空'
            }), 400
        
        success = send_test_email(email)
        if success:
            return jsonify({
                'code': 0,
                'message': '测试邮件已发送'
            })
        else:
            return jsonify({
                'code': -1,
                'message': '邮件发送失败'
            }), 500
    except Exception as e:
        return jsonify({
            'code': -1,
            'message': str(e)
        }), 500


# =============================================
# 启动主程序
# =============================================

if __name__ == '__main__':
    init_db()
    
    mqtt_client_id = f'python-mqtt-{int(os.getpid())}'
    client = mqtt_client.Client(
        callback_api_version=mqtt_client.CallbackAPIVersion.VERSION1,
        client_id=mqtt_client_id
    )
    client.username_pw_set('esp32', 'esp32')
    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(MQTT_BROKER, MQTT_PORT)
    client.loop_start()
    
    app.run(host='0.0.0.0', port=5000, debug=False)