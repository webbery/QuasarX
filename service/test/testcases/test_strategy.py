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

    @pytest.mark.timeout(600)
    def test_run_backtest(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        kwargs['json'] = {
            "name": "basic",
            "level": "T+1",
            "static": ["MACD_5", "sharp"]
        }
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        assert 'features' in data
        assert len(data['features']) > 0
        features = data['features']
        for symbol_feature in features:
            assert 'MACD_5' in symbol_feature
            macd5 = symbol_feature['MACD_5']
            assert len(macd5) > 0
            assert 'sharp' in symbol_feature
            assert 'buy' in data
            assert len(data['buy']) > 0
            assert 'sell' in data
            assert len(data['sell']) > 0

            break
