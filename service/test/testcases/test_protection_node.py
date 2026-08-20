#!/usr/bin/env python3
"""
ProtectionNode 集成测试 (L3)

验证风控保护节点在回测中的触发行为：
  - 止损：下跌行情中 5% 止损应触发
  - 止盈：上涨行情中 5% 止盈应触发
  - 无触发：宽松参数下不应产生事件
  - 事件字段完整性：bar/datetime/symbol/type/entry_price/current_price

复用 metric_test_data/ 已有的标的数据（sz.900001 上涨、sz.900002 下跌），
通过调整保护器参数来控制触发。

使用方法：
  pytest test_protection_node.py -v
"""

import json
import pytest
from tool import load_strategy, run_backtest


# ============================================================
# 测试类
# ============================================================

class TestProtectionStopLoss:
    """止损保护：下跌行情 (sz.900002, -0.3%/day) + 5% 止损"""

    def test_stop_loss_triggers(self, headers):
        """下跌行情中 5% 止损应被触发"""
        strategy = load_strategy("stop_loss_protection_strategy.json")
        result = run_backtest(strategy, headers)

        events = result.get("protection_events", [])
        assert len(events) > 0, "下跌行情 + 5%止损应产生触发事件"

        for evt in events:
            assert evt["type"] == "stop_loss", f"期望 stop_loss，实际 {evt['type']}"

    def test_stop_loss_event_fields(self, headers):
        """事件字段完整性"""
        strategy = load_strategy("stop_loss_protection_strategy.json")
        result = run_backtest(strategy, headers)

        events = result.get("protection_events", [])
        assert len(events) > 0

        evt = events[0]
        required_fields = ["bar", "datetime", "symbol", "type", "entry_price", "current_price"]
        for field in required_fields:
            assert field in evt, f"事件缺失字段: {field}"

    def test_stop_loss_price_relationship(self, headers):
        """止损触发时 current_price < entry_price"""
        strategy = load_strategy("stop_loss_protection_strategy.json")
        result = run_backtest(strategy, headers)

        events = result.get("protection_events", [])
        assert len(events) > 0

        evt = events[0]
        assert evt["current_price"] < evt["entry_price"], (
            f"止损时 current_price({evt['current_price']}) 应 < entry_price({evt['entry_price']})"
        )

    def test_stop_loss_approximate_threshold(self, headers):
        """止损触发时跌幅接近 5%（容差 2%）"""
        strategy = load_strategy("stop_loss_protection_strategy.json")
        result = run_backtest(strategy, headers)

        events = result.get("protection_events", [])
        assert len(events) > 0

        evt = events[0]
        if evt["entry_price"] > 0:
            loss_pct = (evt["entry_price"] - evt["current_price"]) / evt["entry_price"]
            assert 0.03 <= loss_pct <= 0.07, (
                f"止损跌幅 {loss_pct:.2%} 应在 3%~7% 范围（设定 5%）"
            )


class TestProtectionTakeProfit:
    """止盈保护：上涨行情 (sz.900001, +0.29%/day) + 5% 止盈"""

    def test_take_profit_triggers(self, headers):
        """上涨行情中 5% 止盈应被触发"""
        strategy = load_strategy("take_profit_protection_strategy.json")
        result = run_backtest(strategy, headers)

        events = result.get("protection_events", [])
        assert len(events) > 0, "上涨行情 + 5%止盈应产生触发事件"

        for evt in events:
            assert evt["type"] == "take_profit", f"期望 take_profit，实际 {evt['type']}"

    def test_take_profit_price_relationship(self, headers):
        """止盈触发时 current_price > entry_price"""
        strategy = load_strategy("take_profit_protection_strategy.json")
        result = run_backtest(strategy, headers)

        events = result.get("protection_events", [])
        assert len(events) > 0

        evt = events[0]
        assert evt["current_price"] > evt["entry_price"], (
            f"止盈时 current_price({evt['current_price']}) 应 > entry_price({evt['entry_price']})"
        )


class TestProtectionNoTrigger:
    """无触发：上涨行情 + 90% 止损（极宽松，不应触发）"""

    def test_no_events_with_loose_params(self, headers):
        """宽松参数下 protection_events 应为空"""
        strategy_json = json.loads(load_strategy("stop_loss_protection_strategy.json"))

        # 修改标的为上涨标的，止损比例改为 90%
        for node in strategy_json["nodes"]:
            if node["data"]["nodeType"] == "input":
                node["data"]["params"]["code"]["value"] = ["sz.900001"]
            if node["data"]["nodeType"] == "signal":
                node["data"]["params"]["code"]["value"] = ["sz.900001"]
            if node["data"]["nodeType"] == "protection":
                node["data"]["params"]["stop_loss"]["percent"] = 0.90

        result = run_backtest(json.dumps(strategy_json), headers)
        events = result.get("protection_events", [])
        assert len(events) == 0, f"宽松参数不应触发，实际触发 {len(events)} 次: {events}"


class TestProtectionNoProtectionNode:
    """无 Protection 节点时 protection_events 不存在"""

    def test_no_events_without_protection_node(self, headers):
        """不含 Protection 节点的策略不应有 protection_events"""
        strategy = load_strategy("up_trend_strategy.json")
        result = run_backtest(strategy, headers)

        events = result.get("protection_events")
        assert events is None or len(events) == 0, (
            "无 Protection 节点时不应有 protection_events"
        )
