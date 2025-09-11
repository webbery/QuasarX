import requests
import subprocess
import sys
import pytest

if __name__ == "__main__":
    # 启动本地服务
    # result = subprocess.popen(['QuantHive', 'config.json'], capture_output=True)
    # if result.returncode != 0:

    #     return
    if len(sys.argv) > 1:
        testfile = sys.argv[1]
        if len(sys.argv) == 2:
            result = subprocess.run(["pytest", "testcases/test_" + testfile.lower() + ".py"], check=False)
        elif len(sys.argv) == 3:
            testcase = sys.argv[2]
            result = subprocess.run(["pytest", "testcases/test_" + testfile.lower() + ".py::Test" + testfile + "::" + testcase], check=False)
    else:
        # 开始接口测试
        result = subprocess.run(["pytest", "testcases/"], check=False)

    # 退出
    exit_uri = "http://localhost:19107/v0" + "/exit"
    response = requests.post(exit_uri)
    