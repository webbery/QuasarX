import requests
import sys
from tool import check_response, BASE_URL
import pytest

@pytest.mark.usefixtures("auth_token")
class TestStrategy:
    @pytest.mark.timeout(5)
    def test_strategy(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        assert len(data) > 0

    # @pytest.mark.timeout(5)
    # def test_run_backtest(self):
    #     payload = {
    #         "name": "xgboost",
    #         "static": ["MACD_5"]
    #     }
    #     response = requests.post(f"{BASE_URL}/backtest", json=payload)
    #     data = check_response(response)
