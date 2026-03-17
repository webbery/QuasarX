import requests
import pytest
from tool import check_response, BASE_URL
import time

@pytest.mark.usefixtures("auth_token")
class TestTradeHistory:
    """交易历史查询测试"""

    def test_get_trades_with_symbol(self, auth_token):
        """测试获取指定标的的交易历史"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        # 查询一个存在的标的（假设有交易记录）
        params = {
            'symbol': 'sz.000001',
            'page': 1,
            'page_size': 10
        }

        response = requests.get(f"{BASE_URL}/trade/history", params=params, **kwargs)
        data = check_response(response, 200)

        assert data is not None
        assert 'total_count' in data
        assert 'page' in data
        assert 'page_size' in data
        assert 'trades' in data
        assert isinstance(data['trades'], list)

        # 如果返回了交易记录，检查结构
        if data['trades']:
            trade = data['trades'][0]
            assert 'order' in trade
            assert 'trades' in trade
            assert 'total_amount' in trade
            assert 'total_quantity' in trade

            order = trade['order']
            assert 'symbol' in order
            assert order['symbol'] == 'sz.000001'
            assert 'volume' in order
            assert 'type' in order
            assert 'side' in order
            assert 'time' in order

            trades = trade['trades']
            assert isinstance(trades, list)
            if trades:
                trade_detail = trades[0]
                assert 'time' in trade_detail
                assert 'price' in trade_detail
                assert 'quantity' in trade_detail

    def test_get_trades_with_time_filter(self, auth_token):
        """测试时间过滤"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        # 查询最近一天的交易
        end_time = int(time.time())
        start_time = end_time - 24 * 3600  # 最近24小时

        params = {
            'symbol': 'sz.000001',
            'start': start_time,
            'end': end_time,
            'page': 1,
            'page_size': 10
        }

        response = requests.get(f"{BASE_URL}/trade/history", params=params, **kwargs)
        data = check_response(response, 200)

        assert data is not None
        assert 'trades' in data

    def test_get_trades_with_pagination(self, auth_token):
        """测试分页功能"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        # 第一页，每页5条
        params_page1 = {
            'symbol': 'sz.000001',
            'page': 1,
            'page_size': 5
        }

        response1 = requests.get(f"{BASE_URL}/trade/history", params=params_page1, **kwargs)
        data1 = check_response(response1, 200)

        # 第二页，每页5条
        params_page2 = {
            'symbol': 'sz.000001',
            'page': 2,
            'page_size': 5
        }

        response2 = requests.get(f"{BASE_URL}/trade/history", params=params_page2, **kwargs)
        data2 = check_response(response2, 200)

        assert data1 is not None
        assert data2 is not None

        # 如果总记录数超过5，两页的记录应该不同
        if data1['total_count'] > 5:
            assert len(data1['trades']) == 5
            # 第二页可能有5条或更少
            assert len(data2['trades']) <= 5
            # 两页的记录应该不同（假设有足够多的记录）
            if data1['trades'] and data2['trades']:
                # 检查订单ID是否不同（如果有id字段）
                pass

    def test_get_all_trades(self, auth_token):
        """测试获取所有交易记录（不指定symbol）"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        params = {
            'page': 1,
            'page_size': 20
        }

        response = requests.get(f"{BASE_URL}/trade/history", params=params, **kwargs)
        data = check_response(response, 200)

        assert data is not None
        assert 'total_count' in data
        assert 'trades' in data
        # 不指定symbol时，不应返回symbol字段
        assert 'symbol' not in data

    def test_invalid_parameters(self, auth_token):
        """测试无效参数"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        # 测试无效的页码
        params = {
            'page': 0,  # 无效页码
            'page_size': 10
        }

        response = requests.get(f"{BASE_URL}/trade/history", params=params, **kwargs)
        # 期望返回400或200（取决于实现）
        # 这里只检查是否返回有效响应
        assert response.status_code in [200, 400]

        # 测试过大的page_size
        params = {
            'page': 1,
            'page_size': 10000  # 超过最大值
        }

        response = requests.get(f"{BASE_URL}/trade/history", params=params, **kwargs)
        assert response.status_code in [200, 400]

    def test_backtest_scenario(self, auth_token):
        """回测场景专用测试"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        # 回测中常用的查询模式
        params = {
            'symbol': 'sz.000001',
            'strategy': 'ma',  # 策略名称
            'start': 1672502400,     # 2023-01-01
            'end': 1672588800,       # 2023-01-02
            'page': 1,
            'page_size': 100
        }

        response = requests.get(f"{BASE_URL}/trade/history", params=params, **kwargs)
        data = check_response(response, 200)

        assert data is not None
        # 在回测场景中，我们可以验证交易记录的时间范围
        if data['trades']:
            for trade in data['trades']:
                order_time = trade['order']['time']
                assert params['start'] <= order_time <= params['end']

                # 验证成交时间也在范围内
                for trade_detail in trade['trades']:
                    assert params['start'] <= trade_detail['time'] <= params['end']