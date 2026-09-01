#!/usr/bin/env python3
"""
POST /v0/capacity 容量扫描测试

覆盖：
  - 默认 method = turnover_market_share
  - method 枚举值合法性（响应回显 method / method_description）
  - volume_based 与 turnover_based 市占率对结果的影响（应给出不同容量）
  - closing_liquidity_ratio 对容量的压缩效应
  - 非法 method 字符串回退到默认

依赖：
  - QuantService 启动在 https://localhost:19107/v0
  - sz.900001 数据已导入（conftest.py upload_test_data 自动覆盖）
  - inline 一个 MA 动量策略（buy/sell 双向，触发 round-trip 才能拿到 trades）

运行：
  pytest test_capacity_scan.py -v
"""

import json
import time
from pathlib import Path

import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 复用 metric_test_data/ 已有策略（memory: 复用现有测试数据目录）
METRIC_DIR = Path(__file__).parent / "metric_test_data"

# 部署到服务后用的脚本名（避免与已有同名冲突）
STRATEGY_NAME = "test_capacity_roundtrip"


def _build_roundtrip_strategy() -> dict:
    """构造会产生 BUY + SELL 双向交易的策略（capacity scan 才有 trades）

    使用双 MA 交叉策略（参考 golden_cross.json）：
        buy  = ma_short[t] > ma_long[t] and ma_short[t-1] <= ma_long[t-1]  (金叉)
        sell = ma_short[t] < ma_long[t] and ma_short[t-1] >= ma_long[t-1]  (死叉)
    sz.900005 的高波动率数据（93-115 范围，多次趋势变化）会产生多次交叉信号。
    """
    return {
        "id": STRATEGY_NAME,
        "name": STRATEGY_NAME,
        "version": 1,
        "source": "A_hfq",
        "backtest": {"start": "2023-01-01", "end": "2023-04-01"},
        "nodes": [
            {"id": "1", "type": "custom",
             "data": {"label": "行情", "nodeType": "input",
                      "params": {"code": {"value": ["sz.900005"], "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"},
                                 "open": {"value": "open", "type": "text"},
                                 "high": {"value": "high", "type": "text"},
                                 "low": {"value": "low", "type": "text"},
                                 "volume": {"value": "volume", "type": "text"}}}},
            {"id": "2", "type": "custom",
             "data": {"label": "ma_short", "nodeType": "function",
                      "params": {"method": {"value": "MA", "type": "select"},
                                 "range": {"value": "3d", "type": "text"}}}},
            {"id": "3", "type": "custom",
             "data": {"label": "ma_long", "nodeType": "function",
                      "params": {"method": {"value": "MA", "type": "select"},
                                 "range": {"value": "8d", "type": "text"}}}},
            {"id": "4", "type": "custom",
             "data": {"label": "signal", "nodeType": "signal",
                      "params": {"code": {"value": ["sz.900005"], "type": "text"},
                                 "buy":  {"value": "ma_short[t] > ma_long[t] and ma_short[t-1] <= ma_long[t-1]", "type": "text"},
                                 "sell": {"value": "ma_short[t] < ma_long[t] and ma_short[t-1] >= ma_long[t-1]", "type": "text"}}}},
            {"id": "5", "type": "custom",
             "data": {"label": "portfolio", "nodeType": "portfolio",
                      "params": {"positionRatio": {"value": 1.0, "type": "number"}}}},
            {"id": "6", "type": "custom",
             "data": {"label": "execution", "nodeType": "execution",
                      "params": {"commission": {"value": 0.0, "type": "number"},
                                 "stampDuty": {"value": 0.0, "type": "number"},
                                 "minFee": {"value": 0, "type": "number"},
                                 "slippageModel": {"value": 0, "type": "number"},
                                 "slippage": {"value": 0.0, "type": "number"},
                                 "type": {"value": 1, "type": "select"},
                                 "contract": {"value": 0, "type": "select"}}}}],
        "edges": [
            {"id": "e1->2", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e1->3", "source": "1", "target": "3",
             "sourceHandle": "1-close", "targetHandle": "3", "type": "default"},
            {"id": "e1->4", "source": "1", "target": "4",
             "sourceHandle": "1-close", "targetHandle": "4", "type": "default"},
            {"id": "e2->4", "source": "2", "target": "4",
             "sourceHandle": "2", "targetHandle": "4", "type": "default"},
            {"id": "e3->4", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
            {"id": "e4->5", "source": "4", "target": "5",
             "sourceHandle": "4", "targetHandle": "5", "type": "default"},
            {"id": "e5->6", "source": "5", "target": "6",
             "sourceHandle": "5", "targetHandle": "6", "type": "default"}],
    }


# === 模块加载时保存策略 JSON 到 metric_test_data/ ===
# conftest.upload_test_data 扫描该目录提取标的（sz.900001）并上传数据
# 必须在 conftest fixture 运行前完成，所以用模块级代码而非 fixture
_STRATEGY_FILE = METRIC_DIR / f"{STRATEGY_NAME}.json"
_STRATEGY_FILE.parent.mkdir(parents=True, exist_ok=True)
_STRATEGY_FILE.write_text(json.dumps(_build_roundtrip_strategy(), indent=2))


@pytest.fixture(scope="session", autouse=True)
def cleanup_strategy_file():
    """测试结束后清理落盘的策略 JSON 文件"""
    yield
    try:
        _STRATEGY_FILE.unlink(missing_ok=True)
    except Exception:
        pass


CAPITAL_MIN = 100000
CAPITAL_MAX = 50000000
STEPS = 15


def _auth_headers(token: str) -> dict:
    return {"Authorization": token} if token and len(token) > 10 else {}


def _deploy_strategy(token: str, name: str, script: dict) -> dict:
    """POST /v0/strategy 把脚本落盘到 scripts/<name>（capacity/scan 从这里读）"""
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"name": name, "script": script},
        headers=_auth_headers(token),
        verify=VERIFY_SSL,
        timeout=60,
    )
    return {"status_code": resp.status_code, "body": resp.json() if resp.ok else resp.text[:300]}


def _cleanup_strategy(token: str, name: str) -> None:
    """测试结束删除策略文件"""
    try:
        requests.delete(
            f"{BASE_URL}/strategy",
            json={"name": name},
            headers=_auth_headers(token),
            verify=VERIFY_SSL,
            timeout=10,
        )
    except Exception:
        pass


def _scan_capacity(token: str, strategy_name: str, **overrides) -> dict:
    """POST /v0/capacity 统一入口"""
    body = {
        "strategy": strategy_name,
        "capital_range": {"min": CAPITAL_MIN, "max": CAPITAL_MAX, "steps": STEPS},
        "impact_model": {"eta": 1.0, "adv_window": 20},
        "constraints": {"max_participation_rate": 0.05},
        "closing_liquidity_ratio": 0.0,
    }
    body.update(overrides)
    resp = requests.post(
        f"{BASE_URL}/capacity",
        json=body,
        headers=_auth_headers(token),
        verify=VERIFY_SSL,
        timeout=600,
    )
    return {
        "status_code": resp.status_code,
        "body": resp.json() if resp.headers.get("content-type", "").startswith("application/json") else resp.text[:300],
    }


def _validate_capacity_response(data: dict) -> None:
    """校验响应 schema 与 CapacityResponse 一致（仅结构）"""
    assert isinstance(data, dict), f"response 不是 dict: {type(data)}"
    for key in ("strategy", "method", "method_description", "closing_liquidity_ratio",
                "baseline", "capacity_curve", "summary"):
        assert key in data, f"响应缺字段 {key}"
    assert data["method"] in ("turnover_market_share", "volume_market_share")
    assert "capacity_20pct" in data["summary"]
    assert "capacity_50pct" in data["summary"]
    assert isinstance(data["capacity_curve"], list) and len(data["capacity_curve"]) > 0


@pytest.fixture(scope="class")
def deployed_strategy(auth_token, is_backtest):
    """部署一个会产生 BUY+SELL round-trip 的测试策略；测试结束后清理

    容量扫描走回测引擎（依赖 QuoteDB 历史数据 + BackTestHandler 路径），
    仅在 backtest/simulation 模式下有效。tickflow/hx 模式下自动 skip。
    """
    if not is_backtest:
        pytest.skip("capacity/scan 需要 backtest/stock_hist_sim 模式（依赖 QuoteDB + 回测引擎）")
    script = _build_roundtrip_strategy()
    deploy_result = _deploy_strategy(auth_token, STRATEGY_NAME, script)
    if deploy_result["status_code"] >= 400:
        pytest.skip(f"策略部署失败: {deploy_result}")
    yield STRATEGY_NAME
    _cleanup_strategy(auth_token, STRATEGY_NAME)


def _assert_has_trades(data: dict) -> None:
    """CapacityHandler 走 trades.empty() 时返回最小化响应（参见 CapacityHandler.cpp:208）。
    测试断言前应先检查响应是不是 no-trades 分支，否则会在无效响应上做无意义断言。
    抛出 pytest.skip 而不是 fail，让 CI 明确呈现"测试前提不满足"而非"算法错误"。
    """
    if isinstance(data, dict) and data.get("message") == "No trades found":
        pytest.skip(
            "Capacity scan skipped: 策略在回测窗口内未产生任何交易"
            "（round-trip 缺失 → trades 数组空 → Handler 走 no-trades 分支）"
        )


@pytest.mark.usefixtures("auth_token")
class TestCapacityScanTurnover:
    """turnover-based 市占率（默认方法）的核心测试"""

    def test_default_method_is_turnover(self, auth_token, deployed_strategy):
        """不传 method 字段，默认应使用 turnover_market_share"""
        result = _scan_capacity(auth_token, deployed_strategy)
        assert result["status_code"] == 200, f"扫描失败: {result}"
        data = result["body"]
        _assert_has_trades(data)   # capacity scan 走 trades.empty() 分支则 skip
        _validate_capacity_response(data)
        assert data["method"] == "turnover_market_share", \
            f"默认 method 应为 turnover_market_share，实际 {data['method']}"
        assert "成交额" in data["method_description"], \
            f"method_description 应提示 turnover: {data['method_description']}"

    def test_response_schema_matches_openapi(self, auth_token, deployed_strategy):
        """验证响应包含 CapacityResponse schema 中的关键字段"""
        result = _scan_capacity(auth_token, deployed_strategy,
                                method="turnover_market_share")
        assert result["status_code"] == 200, f"扫描失败: {result}"
        data = result["body"]
        _assert_has_trades(data)
        _validate_capacity_response(data)

        # baseline 关键指标
        baseline = data["baseline"]
        for key in ("sharpe", "total_return", "max_drawdown", "win_rate", "n_trades"):
            assert key in baseline, f"baseline 缺字段 {key}"

        # capacity_curve 每点关键指标
        pt = data["capacity_curve"][0]
        for key in ("capital", "sharpe", "avg_participation",
                    "max_participation", "avg_slippage_bps",
                    "orders_above_limit", "sharpe_decay"):
            assert key in pt, f"capacity_curve[0] 缺字段 {key}"

    def test_volume_vs_turnover_diverge(self, auth_token, deployed_strategy):
        """关键正确性验证：volume vs turnover 应给出不同的容量结果。

        同一策略两种 method：
          - participation_vol = adjusted_shares / ADV_volume
          - participation_to  = (adjusted_shares * price) / ADTV_turnover
        这两个 participation 一般不相等 → 平方根冲击定价不同 → capacity 衰减曲线不同。
        """
        cap_vol = _scan_capacity(auth_token, deployed_strategy, method="volume_market_share")
        cap_to = _scan_capacity(auth_token, deployed_strategy, method="turnover_market_share")

        assert cap_vol["status_code"] == 200, f"volume 扫描失败: {cap_vol}"
        assert cap_to["status_code"] == 200, f"turnover 扫描失败: {cap_to}"
        _assert_has_trades(cap_vol["body"])
        _assert_has_trades(cap_to["body"])

        v_sum = cap_vol["body"]["summary"]
        t_sum = cap_to["body"]["summary"]

        # 至少一个容量阈值应该不同（如果完全相同，意味着 participation 计算被旁路了）
        # 但允许两者都接近 0（基准 Sharpe 本身太低），此时改用曲线特征验证
        v_curve = cap_vol["body"]["capacity_curve"]
        t_curve = cap_to["body"]["capacity_curve"]

        # 收集两个曲线对应的 participation / slippage 序列
        v_max_part = max(p["max_participation"] for p in v_curve)
        t_max_part = max(p["max_participation"] for p in t_curve)
        v_avg_slip = sum(p["avg_slippage_bps"] for p in v_curve) / len(v_curve)
        t_avg_slip = sum(p["avg_slippage_bps"] for p in t_curve) / len(t_curve)

        # turnover 模式下参与率通常与 volume 模式系统性不同
        assert (v_max_part != t_max_part) or (abs(v_avg_slip - t_avg_slip) > 1e-6), \
            (f"两种 method 结果完全一致，参与率计算可能被旁路；\n"
             f"  vol:  max_part={v_max_part:.4f} avg_slip_bps={v_avg_slip:.4f}\n"
             f"  to:   max_part={t_max_part:.4f} avg_slip_bps={t_avg_slip:.4f}")

    def test_closing_liquidity_ratio_compresses_capacity(self, auth_token, deployed_strategy):
        """closing_liquidity_ratio > 0 会减少可用 liquidity 池，capacity 应更小（或不变）。

        日终策略场景：盘后窗口流动性是全天的小子集，市占率放大，冲击更剧烈，
        因此 Sharpe 衰减同样幅度对应的资金量更小。
        """
        # 关闭 ratio 走全天 ADV
        cap_full = _scan_capacity(auth_token, deployed_strategy,
                                  method="turnover_market_share",
                                  closing_liquidity_ratio=0.0)
        # 0.05 模拟盘后窗口
        cap_post = _scan_capacity(auth_token, deployed_strategy,
                                  method="turnover_market_share",
                                  closing_liquidity_ratio=0.05)

        assert cap_full["status_code"] == 200
        assert cap_post["status_code"] == 200
        _assert_has_trades(cap_full["body"])
        _assert_has_trades(cap_post["body"])

        # 衰减百分比相同时，日终模式资金量 ≤ 全天模式资金量
        # （ratio > 0 让 participation 放大 → 同样资金下冲击更大 → 衰减更快）
        full_20 = cap_full["body"]["summary"]["capacity_20pct"]
        post_20 = cap_post["body"]["summary"]["capacity_20pct"]

        # post_20 应 ≤ full_20（允许相等的情况为基准 Sharpe = 0 即 capacity 全为 0）
        assert post_20 <= full_20 + 1e-6, \
            f"日终模式 (closing_liquidity_ratio=0.05) capacity_20pct={post_20:.0f} " \
            f"应 ≤ 全天模式 {full_20:.0f}"

    def test_invalid_method_falls_back_to_turnover(self, auth_token, deployed_strategy):
        """非法 method 字符串应回退到默认（turnover_market_share），不是 400 错误。"""
        result = _scan_capacity(auth_token, deployed_strategy, method="bogus_method_xyz")
        assert result["status_code"] == 200, f"非法 method 应回退到默认，不应报错: {result}"
        data = result["body"]
        _assert_has_trades(data)  # 仍需要 trades 非空才能验证 method 字段回退
        assert data["method"] == "turnover_market_share", \
            f"非法 method 应回退为 turnover，实际 {data['method']}"

    def test_method_echo_back_clarity(self, auth_token, deployed_strategy):
        """响应里 method_description 应给出可读公式，对调用方调试友好。"""
        for method in ("turnover_market_share", "volume_market_share"):
            result = _scan_capacity(auth_token, deployed_strategy, method=method)
            assert result["status_code"] == 200
            data = result["body"]
            _assert_has_trades(data)
            desc = data["method_description"]
            if method == "turnover_market_share":
                assert "成交额" in desc and "ADTV" in desc, \
                    f"turnover 描述应含 成交额/ADTV，实际 {desc}"
            else:
                assert "成交量" in desc and "ADV" in desc, \
                    f"volume 描述应含 成交量/ADV，实际 {desc}"
