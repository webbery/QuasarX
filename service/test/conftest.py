import requests
import pytest

class AuthAPI:
    """模拟认证API"""
    def login(self):
        """模拟登录请求"""
        params = {"name": 'admin', 'pwd': 'admin'}
        response = requests.post(f"http://localhost:19107/v0/user/login", json=params, verify=False)
        # response = requests.post(f"https://47.115.93.62:19107/v0/user/login", json=params, verify=False)
        data = response.json()
        assert isinstance(data, object)
        assert 'mode' in data
        token = data['tk']
        assert len(token) > 0
        return token

@pytest.fixture(scope="session")
def auth_token(request):
    """提供认证API实例"""
    return AuthAPI().login()
    