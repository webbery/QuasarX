import requests
import sys
from tool import check_response, BASE_URL
import pytest
import math

# 订单测试脚本
order_operation_tests = [
    # 买入并取消订单
    [
        {'operation': 'buy', 'code': '600489', 'price': -0.05, 'count': 200, 'condition': None},    # 立即以当前价+0.01买入200股
        {'operation': 'cancel', 'code': '600489', 'count': 200, 'condition': 'wait'},    # 订单处于等待状态则取消订单
    ],
    # 卖出股票
    [
        {'operation': 'buy', 'code': '600489', 'price': +0.01, 'count': 200, 'condition': 'empty'},    # 如果持仓股票不存在立即以当前价+0.01买入200股
        {'operation': 'sell', 'code': '600489', 'price': +0.00, 'count': 200, 'condition': '1d'}    # 如果存在1天前的持仓则卖出200股股票
    ]
    # 暂停下单
    # 以市价单买入并卖出
]

@pytest.mark.usefixtures("auth_token")
class TestOrder:
    stock_id = '000001'
    order_id = -1
    sys_id = '12002P900000001'

    option_id = '10010462'
    option_price = 0.2730
    option_order_id = -1
    option_sys_id = ''

    def generate_args(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        return kwargs
    
    def get_stock(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {"id": self.stock_id}
        response = requests.get(f"{BASE_URL}/stocks/detail", params=params, **kwargs)
        return check_response(response)

    def get_etf_option(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {"id": self.option_id}
        response = requests.get(f"{BASE_URL}/options/detail", params=params, **kwargs)
        return check_response(response)

    def limit_order_buy_stock(self, auth_token):
        stock_info = self.get_stock(auth_token)
        cur_price = stock_info['price']
        lower_price = stock_info['lower']
        upper_price = stock_info['upper']
        # 希望以较低的价格下单
        offset = (upper_price - lower_price) / 2
        mid_price = lower_price + offset
        order_price = max(lower_price, min(cur_price - offset, mid_price))
        if order_price < 0.1:
            order_price = 11.0

        kwargs = self.generate_args(auth_token)
        params = {"symbol": self.stock_id, 'type': 1, 'quantity': 200, 'prices': [order_price],
                  'direct': 0, 'kind': 0, 'timeType': 0}
        response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
        return check_response(response)

    def market_order_sell_stock(self, auth_token):
        kwargs = self.generate_args(auth_token)
        
        params = {"symbol": self.stock_id, 'type': 0, 'kind': 0, 'quantity': 100, 'prices': [999,1000],
                  'direct': 1}
        response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
        return check_response(response)

    def get_stock_orders(self, auth_token):
        kwargs = self.generate_args(auth_token)
        response = requests.get(f"{BASE_URL}/trade/order", **kwargs)
        return check_response(response)

    def get_option_orders(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {
            "type": 1
        }
        response = requests.get(f"{BASE_URL}/trade/order", params = params, **kwargs)
        return check_response(response)
    
    def cancel_order(self, auth_token):
        pass

    def market_order_buy_option(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {"symbol": self.option_id, 'type': 1, 'quantity': 200, 'prices': [1.0],
                  'direct': 0, 'kind': 1, 'open': 0, 'hedge': 0, 'timeType': 0}
        response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
        return check_response(response)
    
    def market_order_sell_option(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {"symbol": self.option_id, 'type': 1, 'quantity': 200, 'prices': [1.0],
                  'direct': 1, 'kind': 1}
        response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
        return check_response(response)
    
    @pytest.mark.timeout(60)
    def test_stock_order_buy(self, auth_token):
        data = self.limit_order_buy_stock(auth_token=auth_token)
        assert isinstance(data, object)
        assert "id" in data
        assert data['id'] != -1
        self.order_id = data['id']
        self.sys_id = data['sysID']

    @pytest.mark.timeout(60)
    def test_get_all_stock_orders(self, auth_token):
        data = self.get_stock_orders(auth_token=auth_token)
        assert isinstance(data, list)
        assert len(data) == 1
        for item in data:
            print(item["sysID"], item["status"])
            assert isinstance(item, object)
            assert "id" in item
            assert "kind" in item
            assert "type" in item
            assert "prices" in item
            assert isinstance(item["prices"], list)
            assert "quantity" in item
            assert "direct" in item
            assert "status" in item
            assert "sysID1" in item
            break

    @pytest.mark.timeout(60)
    def test_cancel_stock_order(self, auth_token):
        kwargs = self.generate_args(auth_token)
        params = {'id': self.order_id, 'sysID': self.sys_id}
        response = requests.delete(f"{BASE_URL}/trade/order", json=params, **kwargs)
        data = check_response(response)
        data = self.get_stock_orders(auth_token=auth_token)
        # assert len(data) == 0
        if len(data) == 1:
            order = data[0]
            assert order["status"] == 7     # 取消成功

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

    @pytest.mark.timeout(60)
    def test_option_order_buy(self, auth_token):
        data = self.market_order_buy_option(auth_token=auth_token)
        assert isinstance(data, object)
        assert "id" in data
        assert data['id'] != -1
        self.option_id = data['id']
        self.option_sys_id = data['sysID']

    @pytest.mark.timeout(60)
    def test_get_all_option_orders(self, auth_token):
        data = self.get_option_orders(auth_token=auth_token)
        assert isinstance(data, list)

    @pytest.mark.timeout(60)
    def test_cancel_option_order(self, auth_token):
        pass

    @pytest.mark.timeout(60)
    def test_option_order_sell(self, auth_token):
        pass

    @pytest.mark.timeout(60)
    def test_option_order_exe(self, auth_token):
        pass