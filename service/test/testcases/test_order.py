import requests
import sys
from tool import check_response, BASE_URL
import pytest

@pytest.mark.usefixtures("auth_token")
class TestOrder:
    stock_id = '000001'
    order_id = -1
    sys_id = ''

    def generate_args(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        return kwargs
     
    @pytest.mark.timeout(60)
    def test_order_buy(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {"symbol": self.stock_id, 'type': 1, 'quantity': 200, 'price': [11.0, 1.1, 1.2, 1.3, 1.4],
                  'direct': 0, 'kind': 0}
        response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        assert "id" in data
        assert data['id'] != -1
        self.order_id = data['id']
        self.sys_id = data['sysID']

    @pytest.mark.timeout(60)
    def test_get_all_orders(self, auth_token):
        kwargs = self.generate_args(auth_token)

        response = requests.get(f"{BASE_URL}/trade/order", **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        assert len(data) > 0
        for item in data:
            assert isinstance(item, object)
            assert "id" in item
            assert "kind" in item
            assert "type" in item
            assert "prices" in item
            assert isinstance(item["prices"], list)
            assert "quantity" in item
            assert "direct" in item
            assert "status" in item
            assert "sysID" in item
            break

    @pytest.mark.timeout(5)
    def test_cancel_order(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {'id': self.order_id, 'sysID': self.sys_id}
        response = requests.delete(f"{BASE_URL}/trade/order", json=params, **kwargs)
        data = check_response(response)
        

    # @pytest.mark.timeout(5)
    # def test_order_sell(self, auth_token):
    #     kwargs = self.generate_args(auth_token)
        
    #     params = {"symbol": self.stock_id, 'type': 0, 'kind': 0, 'quantity': 100, 'price': [999,1000],
    #               'direct': 1}
    #     response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
    #     data = check_response(response)
    #     assert isinstance(data, object)
    #     assert "id" in data
    #     assert "status" in data