import requests
import time
from tool import check_response, BASE_URL
import pytest


@pytest.mark.usefixtures("auth_token")
class TestReplayTick:
    """Tick 数据存储到 DuckDB 测试 — 验证 RecordHandler 记录 tick 到 DuckDB + /replay 查询"""

    # 测试标的（需与 tickflow.json 中 pool 一致）
    TEST_SYMBOLS = ["600000.SH", "000001.SZ"]

    @pytest.mark.timeout(10)
    def test_replay_endpoint_available(self, auth_token):
        """POST /replay 无参数 → 默认 query action，symbol 缺失返回空数组"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.post(f"{BASE_URL}/replay", **kwargs)
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, list)

    @pytest.mark.timeout(10)
    def test_replay_query_by_symbol(self, auth_token):
        """POST /replay?action=query&symbol=xxx → 返回该 symbol 的 tick 列表"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        symbol = self.TEST_SYMBOLS[0]
        response = requests.post(
            f"{BASE_URL}/replay?action=query&symbol={symbol}",
            **kwargs
        )
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, list)
        # 如果服务运行了一段时间，应该有数据
        # 至少验证响应格式正确
        if len(data) > 0:
            tick = data[0]
            assert "time" in tick
            assert "open" in tick
            assert "close" in tick
            assert "high" in tick
            assert "low" in tick
            assert "volume" in tick
            assert tick["symbol"] == symbol

    @pytest.mark.timeout(10)
    def test_replay_query_with_time_range(self, auth_token):
        """POST /replay?action=query&symbol=xxx&start=xxx&end=xxx → 按时间范围过滤"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        symbol = self.TEST_SYMBOLS[0]
        # 查询未来时间 → 应返回空
        future_ts = int(time.time()) + 86400
        response = requests.post(
            f"{BASE_URL}/replay?action=query&symbol={symbol}&start={future_ts}",
            **kwargs
        )
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, list)
        assert len(data) == 0

    @pytest.mark.timeout(10)
    def test_replay_query_limit(self, auth_token):
        """POST /replay?action=query&limit=N → 返回不超过 N 条"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.post(
            f"{BASE_URL}/replay?action=query&limit=5",
            **kwargs
        )
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, list)
        assert len(data) <= 5

    @pytest.mark.timeout(10)
    def test_replay_query_all_symbols(self, auth_token):
        """POST /replay?action=query (无 symbol) → 返回所有标的 tick"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.post(
            f"{BASE_URL}/replay?action=query&limit=100",
            **kwargs
        )
        assert response.status_code == 200
        data = response.json()
        assert isinstance(data, list)
        # 验证 tick 结构
        if len(data) > 0:
            tick = data[0]
            assert "time" in tick
            assert "symbol" in tick
            assert "close" in tick
