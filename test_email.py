import smtplib
from email.mime.text import MIMEText

SMTP_HOST = 'smtp.qq.com'
SMTP_PORT = 465

# ===== 替换成你的真实信息 =====
FROM_EMAIL = 'have6666@qq.com'        # 改成你的QQ号@qq.com
SMTP_PASSWORD = 'swarvhfzetbcbjed'     # 改成你获取的16位授权码
TO_EMAIL ='have6666@qq.com'       # 改成接收邮件的邮箱
# ================================

try:
    msg = MIMEText('这是一封来自智能门铃系统的测试邮件', 'plain', 'utf-8')
    msg['From'] = FROM_EMAIL
    msg['To'] = TO_EMAIL
    msg['Subject'] = '智能门铃紧急测试'
    
    with smtplib.SMTP_SSL(SMTP_HOST, SMTP_PORT) as server:
        server.login(FROM_EMAIL, SMTP_PASSWORD)
        server.sendmail(FROM_EMAIL, [TO_EMAIL], msg.as_string())
    print('✅ 邮件发送成功！')
except Exception as e:
    print(f'❌ 失败: {e}')
