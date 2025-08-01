import smtplib
from email.mime.text import MIMEText
from email.header import Header
import traceback
import ssl
import sys

if __name__ == "__main__":
    if len(sys.argv) != 5:
        print('Usage: sender passwd reciever message')
        exit(0)

    sender = sys.argv[1]
    passwd = sys.argv[2]
    reciever = sys.argv[3]
    msg_info = sys.argv[4]
    context = ssl.create_default_context()
    context.options |= ssl.OP_NO_TLSv1
    context.options |= ssl.OP_NO_TLSv1_1
    try:
        with smtplib.SMTP("smtp.qq.com", 587) as server:
            server.starttls(context=context)

            msg=MIMEText(msg_info,'plain','utf-8')
            msg['From']=sender  # 括号里的对应发件人邮箱昵称、发件人邮箱账号
            msg['To']=reciever              # 括号里的对应收件人邮箱昵称、收件人邮箱账号
            msg['Subject']="Quant Operator Inform"         # 邮件的主题，也可以说是标题
    
            server.login(sender, passwd)  # 括号中对应的是发件人邮箱账号、邮箱密码
            server.sendmail(sender,[reciever,],msg.as_string())  # 括号中对应的是发件人邮箱账号、收件人邮箱账号、发送邮件
            server.quit()  # 关闭连接
    except Exception as e:  # 如果 try 中的语句没有执行，则会执行下面的 ret=False
        print('send email fail:', e.args)
        print('=========')
        print(traceback.format_exc())