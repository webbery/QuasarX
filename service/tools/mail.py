import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart
from email.mime.base import MIMEBase
from email import encoders
from email.header import Header
from email.utils import formataddr
import traceback
import ssl
import sys
import os

if __name__ == "__main__":
    if len(sys.argv) < 5:
        print('Usage: sender passwd receiver body_or_file [content_type] [subject] [attach1] [attach2] ...')
        exit(0)

    sender = sys.argv[1]
    passwd = sys.argv[2]
    receiver = sys.argv[3]
    body_arg = sys.argv[4]
    content_type = sys.argv[5] if len(sys.argv) > 5 else 'plain'
    subject = sys.argv[6] if len(sys.argv) > 6 else "Quant Operator Inform"
    attachments = sys.argv[7:] if len(sys.argv) > 7 else []

    # 第 4 参数：如果是现有文件路径则读取文件内容，否则直接作为消息文本（向后兼容）
    if os.path.isfile(body_arg):
        try:
            with open(body_arg, 'r', encoding='utf-8') as f:
                body_content = f.read()
        except Exception as e:
            print('read body file failed:', e.args)
            exit(1)
    else:
        body_content = body_arg

    # 构建邮件
    if attachments:
        # 有附件时用 MIMEMultipart
        msg = MIMEMultipart('mixed')
        msg.attach(MIMEText(body_content, content_type, 'utf-8'))
        
        # 添加附件
        total_size = 0
        max_total_mb = 20.0
        for attach_path in attachments:
            if not os.path.isfile(attach_path):
                print(f'WARNING: attachment not found, skipping: {attach_path}')
                continue
            
            file_size = os.path.getsize(attach_path)
            if total_size + file_size > max_total_mb * 1024 * 1024:
                print(f'WARNING: attachment exceeds size limit ({max_total_mb}MB), skipping: {attach_path}')
                continue
            
            try:
                with open(attach_path, 'rb') as f:
                    part = MIMEBase('application', 'octet-stream')
                    part.set_payload(f.read())
                    encoders.encode_base64(part)
                    
                    # RFC2231 编码文件名（CJK 安全）
                    filename = os.path.basename(attach_path)
                    part.add_header(
                        'Content-Disposition',
                        'attachment',
                        filename=('utf-8', '', filename)
                    )
                    msg.attach(part)
                    total_size += file_size
                    print(f'Attached: {filename} ({file_size} bytes)')
            except Exception as e:
                print(f'WARNING: failed to attach {attach_path}: {e.args}')
    else:
        # 无附件时用纯 MIMEText
        msg = MIMEText(body_content, content_type, 'utf-8')

    msg['From'] = sender
    msg['To'] = receiver
    msg['Subject'] = Header(subject, 'utf-8')

    context = ssl.create_default_context()
    try:
        with smtplib.SMTP("smtp.qq.com", 587) as server:
            server.starttls(context=context)
            server.login(sender, passwd)
            server.sendmail(sender, [receiver], msg.as_string())
            server.quit()
            print(f'Email sent successfully to {receiver}')
    except Exception as e:
        print('send email failed:', e.args)
        print('=========')
        print(traceback.format_exc())
        sys.exit(1)
