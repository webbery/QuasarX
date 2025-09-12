from tool import check_response, BASE_URL
import pytest
import requests

@pytest.mark.usefixtures("auth_token")
class TestUser:
    @pytest.mark.timeout(5)
    def test_status(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/server/status", **kwargs)
        data = check_response(response)
        assert 'cpu' in data
        assert 'mem' in data

    @pytest.mark.timeout(5)
    def test_index(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/index/quote", **kwargs)
        data = check_response(response)
