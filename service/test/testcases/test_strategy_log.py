import requests
import sys
import time
from tool import check_response, BASE_URL
import pytest


@pytest.mark.usefixtures("auth_token")
class TestStrategyLog:
    """策略日志 DuckDB 写入与查询测试

    验证 FlowSubsystem::StartRealtime 等路径中 STRATEGY_INFO 宏
    写入的日志能否通过 StrategyLogHandler::get 正确查询。

    关键测试点：
    1. DuckDBLogger 初始化状态
    2. 日志写入后延迟查询（等待后台 worker flush）
    3. 日志内容正确性
    """

    def _auth_kwargs(self, auth_token):
        kwargs = {"verify": False}
        if auth_token and len(auth_token) > 10:
            kwargs["headers"] = {"Authorization": auth_token}
        return kwargs

    def get_strategy_logs(self, auth_token, **params):
        """查询策略日志"""
        kwargs = self._auth_kwargs(auth_token)
        kwargs["params"] = params
        response = requests.get(f"{BASE_URL}/strategy/logs", **kwargs)
        return check_response(response)

    def get_strategy_stats(self, auth_token):
        """获取策略日志统计"""
        kwargs = self._auth_kwargs(auth_token)
        kwargs["params"] = {"type": "stats"}
        response = requests.get(f"{BASE_URL}/strategy/logs", **kwargs)
        return check_response(response)

    def get_strategies_list(self, auth_token):
        """获取有日志的策略列表"""
        kwargs = self._auth_kwargs(auth_token)
        kwargs["params"] = {"type": "strategies"}
        response = requests.get(f"{BASE_URL}/strategy/logs", **kwargs)
        return check_response(response)

    @pytest.mark.timeout(10)
    def test_duckdb_initialized(self, auth_token):
        """验证 DuckDBLogger 已初始化：stats 接口应返回 503 如果未初始化"""
        kwargs = self._auth_kwargs(auth_token)
        kwargs["params"] = {"type": "stats"}
        response = requests.get(f"{BASE_URL}/strategy/logs", **kwargs)
        # 503 表示未初始化，200 表示正常
        assert response.status_code != 503, "DuckDBLogger 未初始化"
        data = check_response(response)
        assert "total_logs" in data, "stats 响应应包含 total_logs 字段"

    @pytest.mark.timeout(30)
    def test_strategy_log_written_after_start_realtime(self, auth_token, is_backtest):
        """启动策略后验证日志写入 DuckDB

        流程：
        1. 部署一个简单策略（触发 StartRealtime / StartBacktest）
        2. 等待一段时间让后台 worker flush 日志到 DuckDB
        3. 查询日志，验证数量 > 0
        """
        if is_backtest:
            pytest.skip("回测模式下不启动实时策略，跳过日志写入测试")

        # 使用已有的测试策略脚本
        import json
        import os
        script_path = "./script/ma_graph_strategy.json"
        if not os.path.exists(script_path):
            pytest.skip(f"策略脚本不存在: {script_path}")

        with open(script_path, "r", encoding="utf-8") as f:
            script = json.load(f)

        test_name = "test_log_write"

        # 先清理可能残留的同名策略
        kwargs = self._auth_kwargs(auth_token)
        kwargs["json"] = {"mode": 2, "name": test_name}
        requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        kwargs["json"] = {"name": test_name}
        requests.delete(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        time.sleep(0.5)

        # 部署策略（会触发 FlowSubsystem::StartRealtime 及 STRATEGY_INFO）
        kwargs["json"] = {"mode": 0, "name": test_name, "script": script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=30)
        data = check_response(resp)
        assert data["running"] is True, "策略应成功部署并运行"

        # ★ 关键：等待后台 worker flush 日志到 DuckDB
        # DuckDBLogger::worker_loop 每 500ms 或满 200 条 flush 一次
        # 额外等待确保数据落盘
        time.sleep(2)

        # 查询该策略的日志
        logs_data = self.get_strategy_logs(auth_token, type="default", strategy=test_name)

        # 验证日志数量 > 0
        total = logs_data.get("total", 0)
        logs = logs_data.get("logs", [])
        assert total > 0, (
            f"策略 '{test_name}' 启动后应至少有一条日志写入 DuckDB，"
            f"但 count_strategy_logs 返回 {total}。"
            f"可能原因：DuckDBLogger worker_loop 尚未 flush，或 DuckDB 未初始化"
        )
        assert len(logs) > 0, "查询应返回至少一条日志记录"

        # 验证日志内容
        first_log = logs[0]
        assert "timestamp" in first_log, "日志应包含 timestamp"
        assert "strategy" in first_log, "日志应包含 strategy"
        assert "message" in first_log, "日志应包含 message"
        assert "level" in first_log, "日志应包含 level"
        assert first_log["strategy"] == test_name, f"日志策略名称应为 '{test_name}'"

        # 停止并清理
        kwargs["json"] = {"mode": 2, "name": test_name}
        requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        kwargs["json"] = {"name": test_name}
        requests.delete(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        time.sleep(0.5)

    @pytest.mark.timeout(15)
    def test_strategy_stats_after_start(self, auth_token, is_backtest):
        """验证策略启动后 stats 接口返回正确的统计信息"""
        if is_backtest:
            pytest.skip("回测模式下跳过")

        import json
        import os
        script_path = "./script/ma_graph_strategy.json"
        if not os.path.exists(script_path):
            pytest.skip(f"策略脚本不存在: {script_path}")

        with open(script_path, "r", encoding="utf-8") as f:
            script = json.load(f)

        test_name = "test_log_stats"

        # 清理
        kwargs = self._auth_kwargs(auth_token)
        kwargs["json"] = {"mode": 2, "name": test_name}
        requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        kwargs["json"] = {"name": test_name}
        requests.delete(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        time.sleep(0.5)

        # 部署
        kwargs["json"] = {"mode": 0, "name": test_name, "script": script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=30)
        check_response(resp)

        # 等待 flush
        time.sleep(2)

        # 查询 stats
        stats = self.get_strategy_stats(auth_token)
        assert stats["total_logs"] > 0, "stats.total_logs 应 > 0"

        # 验证策略列表中能找到该策略
        strategies = self.get_strategies_list(auth_token)
        strategy_names = [s["name"] for s in strategies.get("strategies", [])]
        assert test_name in strategy_names, (
            f"策略 '{test_name}' 应在有日志的策略列表中，当前列表: {strategy_names}"
        )

        # 清理
        kwargs["json"] = {"mode": 2, "name": test_name}
        requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        kwargs["json"] = {"name": test_name}
        requests.delete(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        time.sleep(0.5)

    @pytest.mark.timeout(15)
    def test_strategy_log_immediate_query_too_early(self, auth_token, is_backtest):
        """验证立即查询（不等待 flush）可能返回 0 条日志

        这个测试确认了 DuckDBLogger 的异步写入特性：
        日志入队后到真正写入 DuckDB 之间存在延迟。
        """
        if is_backtest:
            pytest.skip("回测模式下跳过")

        import json
        import os
        script_path = "./script/ma_graph_strategy.json"
        if not os.path.exists(script_path):
            pytest.skip(f"策略脚本不存在: {script_path}")

        with open(script_path, "r", encoding="utf-8") as f:
            script = json.load(f)

        test_name = "test_log_immediate_query"

        # 清理
        kwargs = self._auth_kwargs(auth_token)
        kwargs["json"] = {"mode": 2, "name": test_name}
        requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        kwargs["json"] = {"name": test_name}
        requests.delete(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        time.sleep(0.5)

        # 部署
        kwargs["json"] = {"mode": 0, "name": test_name, "script": script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=30)
        check_response(resp)

        # 立即查询（不等待 flush）
        logs_data = self.get_strategy_logs(auth_token, type="default", strategy=test_name)
        immediate_total = logs_data.get("total", 0)

        # 注意：这里不 assert > 0，因为立即查询可能返回 0
        # 这个测试的目的是记录"立即查询可能为 0"这一行为
        print(f"\n[StrategyLog] 立即查询返回 total={immediate_total}（可能为 0，因 worker 尚未 flush）")

        # 等待后再次查询
        time.sleep(2)
        logs_data = self.get_strategy_logs(auth_token, type="default", strategy=test_name)
        delayed_total = logs_data.get("total", 0)

        # 延迟查询应 > 0
        assert delayed_total > 0, (
            f"等待 2s 后查询策略 '{test_name}' 日志，仍返回 {delayed_total} 条。"
            f"DuckDBLogger 可能未正确初始化或 worker_loop 异常"
        )

        # 清理
        kwargs["json"] = {"mode": 2, "name": test_name}
        requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        kwargs["json"] = {"name": test_name}
        requests.delete(f"{BASE_URL}/strategy", **kwargs, timeout=5)
        time.sleep(0.5)
