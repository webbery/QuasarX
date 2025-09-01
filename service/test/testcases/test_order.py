import requests
import sys
from tool import check_response, BASE_URL
import pytest

class TestStock:
    stock_id = '000001'
    @pytest.mark.timeout(5)
    def test_order_buy(self):
        params = {"symbol": self.stock_id, 'type': 0, 'quantity': 100, 'price': [1.0,1.1,1.2,1.3,1.4]}
        response = requests.post(f"{BASE_URL}/order/buy", json=params)
        data = check_response(response)
        assert isinstance(data, object)
        if "reports" in data:
            for item in data["reports"]:
                assert "time" in item
                assert "price" in item
                assert "quantity" in item
                break

    @pytest.mark.timeout(5)
    def test_order_sell(self):
        params = {"symbol": self.stock_id, 'type': 0, 'quantity': 100, 'price': [1.0,1.1,1.2,1.3,1.4]}
        response = requests.post(f"{BASE_URL}/order/sell", json=params)
        data = check_response(response)
        assert isinstance(data, object)
        if "reports" in data:
            for item in data["reports"]:
                assert "time" in item
                assert "price" in item
                assert "quantity" in item
                break