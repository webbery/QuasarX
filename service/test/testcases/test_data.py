import requests
import sys
from tool import check_response, BASE_URL
import pytest

@pytest.mark.usefixtures("auth_token")
class TestData:

    @pytest.mark.timeout(10)
    def test_data_sync(self, auth_token):
        kwargs = {
            'stream': True,
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.get(f"{BASE_URL}/data/sync", **kwargs)
        response.raise_for_status()

        assert 'chunked' in response.headers.get('Transfer-Encoding', '').lower()