import requests
import sys
from tool import check_response, BASE_URL
import pytest
from datetime import datetime

@pytest.mark.usefixtures("auth_token")
class TestPrediction:
    @pytest.mark.timeout(5)
    def test_set_operation(self, auth_token):
        today = datetime.now()
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        params = {
            'symbol': '001318',
            'datetime': today.strftime("%Y-%m-%d"),
            'operation': 1,
            'exchange': 0
        }
        response = requests.put(f"{BASE_URL}/predict/operation", json=params, **kwargs)
        check_response(response)

