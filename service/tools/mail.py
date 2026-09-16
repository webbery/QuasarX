import smtplib
from email.mime.text import MIMEText
from email.header import Header
import traceback
import ssl
import sys
import os

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print('Usage: sender passwd receiver message_or_file [html|plain]')
        exit(0)

    sender = sys.argv[1]
    passwd = sys.argv[2]
    receiver = sys.argv[3]
    message_arg = sys.argv[4]
    content_type = sys.argv[5] if len(sys.argv) > 5 else 'plain'

    # 第 4 参数：如果是现有文件路径则读取文件内容，否则直接作为消息文本（向后兼容）
    if os.path.isfile(message_arg):
        try:
            with open(message_arg, 'r', encoding='utf-8') as f:
                msg_info = f.read()
        except Exception as e:
            print('read message file failed:', e.args)
            exit(1)
    else:
        msg_info = message_arg

    context = ssl.create_default_context()
    try:
        with smtplib.SMTP("smtp.qq.com", 587) as server:
            server.starttls(context=context)

            msg = MIMEText(msg_info, content_type, 'utf-8')
            msg['From'] = sender
            msg['To'] = receiver
            msg['Subject'] = "Quant Operator Inform"

            server.login(sender, passwd)
            server.sendmail(sender, [receiver], msg.as_string())
            server.quit()
    except Exception as e:
        print('send email fail:', e.args)
        print('=========')
        print(traceback.format_exc())
