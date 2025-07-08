import requests
import sys
from tool import check_response, BASE_URL
import pytest

# --------------------------
# 测试Risk相关接口
# --------------------------
class TestStopLoss:
    created_id = None
    var_id = None

    @pytest.mark.timeout(5)
    def test_create_stoploss(self):
        payload = {
            "type": 0,
            "email": "15600109607@wo.cn",
            "target": [{
                "price": 48.41,
                "symbol": "001136",
                "type": "AStock",
                "percent": 0.05
            }]
        }
        response = requests.post(f"{BASE_URL}/risk/stoploss", json=payload)
        data = check_response(response)
        assert "id" in data
        self.__class__.created_id = data["id"]
        assert self.__class__.created_id != 0

    @pytest.mark.timeout(5)
    def test_get_stoploss(self):
        response = requests.get(f"{BASE_URL}/risk/stoploss")
        data = check_response(response)
        assert isinstance(data, list)
        if len(data) > 0:
            assert "id" in data[0]

    @pytest.mark.timeout(5)
    def test_delete_stoploss(self):
        if self.created_id:
            params = {"id": self.created_id}
            response = requests.delete(f"{BASE_URL}/risk/stoploss", params=params)
            assert isinstance(response, object)
            assert 'status' in response

    @pytest.mark.timeout(5)
    def test_get_var(self):
        response = requests.get(f"{BASE_URL}/risk/var")
        data = check_response(response)
        assert isinstance(data, list)
        if len(data) > 0:
            assert "id" in data[0]

    @pytest.mark.timeout(5)
    def test_run_var(self):
        payload = {
            "start": "2010-01-01 00:00:00",
            "end": "2030-01-01 00:00:00",
            "portfolio": {
                "strategy": "random",
                "pool": [
                    "000001",
                    "000005"
                ],
                "data_type": "day"
            },
            "confidence": 0.99,
            "days": 10,
            "capital": 10000,
            "commission": {
                "type": "A股",
                "slip": 0.005,
                "min_price": 5,
                "trade_price": 0.0001345,
                "tax": 0.01
            }
        }
        response = requests.post(f"{BASE_URL}/risk/var", json = payload)
        data = check_response(response)

    def test_logic(self):
        pass

    @pytest.mark.timeout(5)
    def test_drawdown(self):
        '''回撤'''
        test_symbols = [['000001']]
        for symbols in test_symbols:
            str_symbols = ','.join(symbols)
            query = 'range=0&symbol=' + str_symbols
            response = requests.get(f"{BASE_URL}/risk/drawdown?" + query)
            data = check_response(response)
            assert isinstance(data, list)

    def test_bussiness(self):
        '''行业集中度'''
        pass

    def test_risklevel(self):
        '''风险等级'''
        pass

    def test_riskcomponents(self):
        '''风险因子分解'''
        pass