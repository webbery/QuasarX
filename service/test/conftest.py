import requests
import pytest
import os
import glob
from pathlib import Path

# 抑制 SSL 警告（本地测试不需要证书验证）
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


@pytest.fixture(scope="session", autouse=True)
def cleanup_test_scripts():
    """
    测试结束后清理 build/scripts/ 目录下 test_* 开头的策略文件。
    避免测试产生的策略脚本残留。
    """
    yield  # 先运行所有测试

    # 测试结束后执行清理
    scripts_dir = Path(__file__).parent.parent / "build" / "scripts"
    if scripts_dir.exists():
        removed = []
        for f in scripts_dir.glob("test_*"):
            f.unlink()
            removed.append(f.name)
        if removed:
            print(f"\n[Cleanup] Removed {len(removed)} test scripts: {', '.join(removed)}")


class AuthAPI:
    """模拟认证API"""
    def __init__(self):
        self._mode = None
        self._token = None

    def login(self):
        """模拟登录请求"""
        params = {"name": 'admin', 'pwd': 'admin'}
        response = requests.post(f"https://localhost:19107/v0/user/login", json=params, verify=False)
        # response = requests.post(f"https://47.115.93.62:19107/v0/user/login", json=params, verify=False)
        data = response.json()
        assert isinstance(data, object)
        assert 'mode' in data
        token = data['tk']
        self._token = token
        self._mode = data['mode']
        assert len(token) > 0
        return token

    @property
    def token(self):
        """认证 token 字符串"""
        return self._token

    @property
    def mode(self):
        """当前服务运行模式，如 'backtest' / 'simulation' / 'realtime'"""
        return self._mode

    def is_backtest_mode(self):
        """判断是否为回测模式"""
        return self._mode and 'backtest' in str(self._mode).lower()


@pytest.fixture(scope="session")
def auth_api():
    """提供 AuthAPI 实例，包含 mode 信息"""
    api = AuthAPI()
    api.login()
    return api


@pytest.fixture(scope="session")
def auth_token(auth_api):
    """提供认证 token 字符串（保持向后兼容）"""
    return auth_api.token


@pytest.fixture(scope="session")
def is_backtest(auth_api):
    """判断当前是否为回测模式"""
    return auth_api.is_backtest_mode()
    