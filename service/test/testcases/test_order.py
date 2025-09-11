import requests
import sys
from tool import check_response, BASE_URL
import pytest

@pytest.mark.usefixtures("auth_token")
class TestStock:
    stock_id = '000001'

    @pytest.mark.timeout(5)
    def test_order_buy(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        params = {"symbol": self.stock_id, 'type': 0, 'quantity': 100, 'price': [1.0,1.1,1.2,1.3,1.4]}
        response = requests.post(f"{BASE_URL}/order/buy", json=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        if "reports" in data:
            for item in data["reports"]:
                assert "time" in item
                assert "price" in item
                assert "quantity" in item
                break

    @pytest.mark.timeout(5)
    def test_order_sell(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        params = {"symbol": self.stock_id, 'type': 0, 'quantity': 100, 'price': [1.0,1.1,1.2,1.3,1.4]}
        response = requests.post(f"{BASE_URL}/order/sell", json=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        if "reports" in data:
            for item in data["reports"]:
                assert "time" in item
                assert "price" in item
                assert "quantity" in item
                break