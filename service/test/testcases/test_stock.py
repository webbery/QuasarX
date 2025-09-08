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
        headers = {
            'Authorization': auth_token
        }
        response = requests.get(f"{BASE_URL}/stocks/simple", headers=headers, verify=False)
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
        headers = {
            'Authorization': auth_token
        }
        params = {"id": self.stock_id}
        response = requests.get(f"{BASE_URL}/stocks/detail", params=params, headers=headers, verify=False)
        data = check_response(response)
        assert isinstance(data, object)
        assert "price" in data
        assert "volume" in data
        assert "turnover" in data

    @pytest.mark.timeout(5)
    def test_stock_history(self, auth_token):
        headers = {
            'Authorization': auth_token
        }
        time_start_str = "2020-1-01"
        time_end_str = "2025-10-01"
        time_start = time.strptime(time_start_str, "%Y-%m-%d")
        start = int(time.mktime(time_start))
        time_end = time.strptime(time_end_str, "%Y-%m-%d")
        end = int(time.mktime(time_end))
        params = {"id": self.stock_id, 'type': 2, 'start': start, 'end': end, 'right': 0}
        response = requests.get(f"{BASE_URL}/stocks/history", params=params, headers=headers, verify=False)
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

    # @pytest.mark.timeout(5)
    # def test_index(self):
    #     query = "?id=000300"
    #     response = requests.get(f"{BASE_URL}/index/quote" + query)
    #     data = check_response(response)
