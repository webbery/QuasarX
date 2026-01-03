import requests
import sys
from tool import check_response, BASE_URL
import pytest
from datetime import datetime
import time

@pytest.mark.usefixtures("auth_token")
class TestStock:
    stock_id = '000001'
    @pytest.mark.timeout(5)
    def test_stock_simple(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/stocks/simple", **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        assert "status" in data
        assert "stocks" in data
        assert isinstance(data["stocks"], list)
        assert len(data["stocks"]) > 0
        assert "symbol" in data["stocks"][0]
        assert "name" in data["stocks"][0]

    @pytest.mark.timeout(5)
    def test_stock_detail(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        params = {"id": self.stock_id}
        response = requests.get(f"{BASE_URL}/stocks/detail", params=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, object)
        assert "price" in data
        assert "volume" in data
        assert "turnover" in data

    @pytest.mark.timeout(5)
    def test_stock_history(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        time_start_str = "2020-1-01"
        time_end_str = "2025-10-01"
        time_start = time.strptime(time_start_str, "%Y-%m-%d")
        start = int(time.mktime(time_start))
        time_end = time.strptime(time_end_str, "%Y-%m-%d")
        end = int(time.mktime(time_end))
        params = {"id": self.stock_id, 'type': '1d', 'start': start, 'end': end, 'right': 0}
        response = requests.get(f"{BASE_URL}/stocks/history", params=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        for row in data:
            assert 'datetime' in row
            assert 'open' in row
            assert 'close' in row
            assert 'low' in row
            assert 'high' in row
            assert 'volume' in row
            break

    @pytest.mark.timeout(20)
    def test_stock_privilege(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        params = {'id': '688719'}
        response = requests.get(f"{BASE_URL}/stocks/privilege", params=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        
    # @pytest.mark.timeout(5)
    # def test_index(self):
    #     query = "?id=000300"
    #     response = requests.get(f"{BASE_URL}/index/quote" + query)
    #     data = check_response(response)

    @pytest.mark.timeout(20)
    def test_sector_today_flow(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        params = {"type": 0}
        response = requests.get(f"{BASE_URL}/stocks/sector/flow", params=params, **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        for row in data:
            assert 'name' in row
            assert 'value' in row
            assert len(row['value']) == 1
            for item in row['value']:
                assert 'date' in item
                assert 'main' in item
                assert 'supbig' in item
                assert 'big' in item
                assert 'mid' in item
                assert 'small' in item
                break
            break
        
    @pytest.mark.timeout(60)
    def test_daily_limit(self, auth_token):
        '''
        单日交易流量限制测试
        
        '''

    @pytest.mark.timeout(20)
    def test_daily_limit(self, auth_token):
        '''
        交易速率限制测试:每秒20笔
        '''
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        symbol = '000001'
        order_price = 11.1
        params = {"symbol": symbol, 'type': 1, 'quantity': 200, 'prices': [order_price],
                'direct': 0, 'kind': 0, 'timeType': 0, 'perf': 100}
        response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
        # data =  check_response(response)