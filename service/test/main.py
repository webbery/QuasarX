import requests
import subprocess

if __name__ == "__main__":
    # 启动本地服务
    # result = subprocess.popen(['QuantHive', 'config.json'], capture_output=True)
    # if result.returncode != 0:

    #     return
    # 开始接口测试
    result = subprocess.run(["pytest", "testcases/"], check=False)

    # 退出
    exit_uri = "http://localhost:19107/v0" + "/exit"
    response = requests.post(exit_uri)
    