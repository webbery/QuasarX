import requests
import sys
from tool import check_response, BASE_URL
import pytest

class TestStock:
    stock_id = '000001'
    @pytest.mark.timeout(5)
    def test_stock_simple(self):
        response = requests.get(f"{BASE_URL}/stocks/simple")
        data = check_response(response)
        assert isinstance(data, object)
        assert "status" in data
        assert "stocks" in data
        assert isinstance(data["stocks"], list)
        assert len(data["stocks"]) > 0
        assert "symbol" in data["stocks"][0]
        assert "name" in data["stocks"][0]

    # @pytest.mark.timeout(5)
    # def test_stock_detail(self):
    #     response = requests.get(f"{BASE_URL}/stocks/detail")
    #     data = check_response(response)
    #     assert isinstance(data, object)
    #     assert "price" in data
    #     assert "volumn" in data

    # @pytest.mark.timeout(5)
    # def test_stock_history(self):
    #     params = {"id": self.stock_id, 'type': 2, 'start': 1588043030, 'end': 1588043030, 'right': 0}
    #     response = requests.get(f"{BASE_URL}/stocks/history", params=params)
    #     data = check_response(response)
    #     assert isinstance(data, list)

    # @pytest.mark.timeout(5)
    # def test_index(self):
    #     query = "?id=000300"
    #     response = requests.get(f"{BASE_URL}/index/quote" + query)
    #     data = check_response(response)
