import requests
import time
from pathlib import Path
from tool import check_response, BASE_URL
import pytest


@pytest.mark.usefixtures("auth_token")
class TestTickRecord:
    """Tick 数据存储测试 — 验证 RecordHandler 记录 tick 到 CBOR/CSV"""

    # 测试标的（需与 tickflow.json 中 pool 一致）
    TEST_SYMBOLS = ["600000.SH", "000001.SZ"]

    @pytest.mark.timeout(10)
    def test_record_endpoint_available(self, auth_token):
        """POST /record 无 symbol 参数 → 400，验证接口存在"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.post(f"{BASE_URL}/record", **kwargs)
        assert response.status_code == 400
        data = response.json()
        assert "error" in data
        assert "symbol" in data["error"].lower()

    @pytest.mark.timeout(10)
    def test_record_query_invalid_symbol(self, auth_token):
        """POST /record?symbol=INVALID → 404，无对应数据目录"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        response = requests.post(f"{BASE_URL}/record?symbol=INVALID", **kwargs)
        assert response.status_code in (400, 404), f"无效 symbol 应返回 400 或 404，实际 {response.status_code}"
        data = response.json()
        assert "error" in data

    @pytest.mark.timeout(10)
    def test_record_query_time_range(self, auth_token):
        """POST /record?symbol=xxx&start=xxx&end=xxx → 验证时间范围过滤"""
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        now = int(time.time())
        yesterday = now - 86400

        for symbol in self.TEST_SYMBOLS:
            response = requests.post(
                f"{BASE_URL}/record?symbol={symbol}&start={yesterday}&end={now}",
                **kwargs
            )
            # 200 或 404 都可接受（可能无数据）
            assert response.status_code in (200, 404), f"{symbol} 查询返回 {response.status_code}"
            if response.status_code == 200:
                data = response.json()
                assert isinstance(data, list)
                # 验证返回的 tick 都在时间范围内
                for tick in data:
                    assert tick.get("time", 0) >= yesterday, f"tick time 小于 start: {tick}"
                    assert tick.get("time", 0) <= now, f"tick time 大于 end: {tick}"

    @pytest.mark.timeout(10)
    def test_daily_directory_exists(self):
        """
        验证 {db_path}/daily/zh/stock/ 目录存在。
        RecordHandler 启动时即创建此目录。
        """
        config_path = Path(__file__).parent.parent / "configs" / "tickflow.json"
        if not config_path.exists():
            pytest.skip("找不到 tickflow 配置文件")

        import json
        with open(config_path) as f:
            cfg = json.load(f)

        db_path = cfg.get("server", {}).get("db_path", "data")
        base_dir = Path(__file__).parent.parent.parent / "build"
        daily_dir = base_dir / db_path / "daily" / "zh" / "stock"

        assert daily_dir.exists(), f"tick 存储目录不存在: {daily_dir}"
        print(f"\n[daily] 目录存在: {daily_dir}")

    @pytest.mark.timeout(30)
    def test_tick_query_accepts_pool_symbol(self, auth_token):
        """
        等待 15s 后查询 pool 中的 symbol。

        结果：
        - 200 + 有数据：tick 已落盘 ✓
        - 404：尚未达到 flush 条件（50条或10分钟超时），也通过

        目的：验证接口行为正常，不强制要求 tick 数量。
        """
        kwargs = {
            'verify': False
        }
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}

        print(f"\n等待 15s 让 TickFlow 推送 tick 数据...")
        time.sleep(15)

        has_any_response = False
        for symbol in self.TEST_SYMBOLS:
            print(f"\n查询 {symbol} 的 tick 记录...")
            response = requests.post(
                f"{BASE_URL}/record?symbol={symbol}",
                **kwargs
            )

            has_any_response = True

            if response.status_code == 200:
                data = check_response(response)
                assert isinstance(data, list)
                tick_count = len(data)
                if tick_count > 0:
                    first_tick = data[0]
                    required_fields = ["time", "open", "close", "high", "low", "volume"]
                    for field in required_fields:
                        assert field in first_tick, f"tick 数据缺少字段: {field}"
                    print(f"  {symbol}: {tick_count} 条 tick")
                    print(f"  首条: time={first_tick['time']}, close={first_tick['close']}")
                else:
                    print(f"  {symbol}: 暂无落盘数据（未达 flush 条件）")
            elif response.status_code == 404:
                print(f"  {symbol}: 暂无 CBOR 文件（未达 flush 条件，正常）")
            else:
                pytest.fail(f"意外状态码: {response.status_code}")

        assert has_any_response, "无任何响应"
