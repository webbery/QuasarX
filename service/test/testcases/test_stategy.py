import requests
import sys
from tool import check_response, BASE_URL
import pytest

class TestStrategy:
    @pytest.mark.timeout(5)
    def test_strategy(self):
        response = requests.get(f"{BASE_URL}/strategy")
        data = check_response(response)
        assert isinstance(data, list)
        assert len(data) > 0

    # @pytest.mark.timeout(5)
    # def test_run(self):
    #     payload = {
    #         "name": "xgboost",
    #         "mode": 1
    #     }
    #     response = requests.post(f"{BASE_URL}/strategy", json=payload)
    #     data = check_response(response)
