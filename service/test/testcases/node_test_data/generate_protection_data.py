#!/usr/bin/env python3
"""
生成风控保护（ProtectionNode）测试策略夹具

输出：
- 策略 JSON: testcases/node_test_data/protection/

为什么风控夹具要单独一个子目录、而且必须进版本库
--------------------------------------------------
早期这些夹具放在 metric_test_data/ 下，而那个目录被 .gitignore 整个忽略。
结果 CI 拉不到文件，tool.load_strategy() 又不报错、只返回一段
`{"status": "error", ...}` 的合法 JSON —— 服务端把它当成"空策略"解析，
回测跑完没有 protection_events，测试报成
"下跌行情 + ATR×2 止损应产生触发事件，实际 events: []"，
排查方向完全被带偏（看起来像风控逻辑没生效，其实是文件没提交）。

所以：
  1. 手写夹具统一由本脚本生成，放在 node_test_data/protection/ 下，
     与生成脚本一起提交，不再受 .gitignore 影响；
  2. tool.load_strategy() 找不到文件时直接抛 FileNotFoundError，
     不再返回"能解析但没内容"的 JSON。

夹具统一拓扑：Input → Signal → Portfolio → Protection → Execution
  - 买入持有（signal buy=true / sell=false），仓位 100%
  - Protection 节点挂在 Portfolio 之后、Execution 之前
  - Execution 成本全 0，排除交易成本对触发时机的干扰

用法：
  cd service/test/testcases/node_test_data
  python generate_protection_data.py
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))
from strategy_builder import StrategyBuilder, protection_node

OUT_DIR = Path(__file__).parent / "protection"

# 下跌行情（-0.3%/day）：用来验证各类止损会触发
DOWN_SYMBOL = "sz.900002"
# 上涨行情（+0.29%/day）：用来验证止损不会误触发
UP_SYMBOL = "sz.900001"

BACKTEST = {"start": "2023-01-01", "end": "2023-04-01"}

# 4 个基础风控器：未启用的也要显式写出来，让夹具自带"当前只测哪一个"的语义
DISABLED_GUARDS = {
    "stop_loss": {"enabled": False, "percent": 0.0},
    "take_profit": {"enabled": False, "percent": 0.0},
    "trailing_stop": {"enabled": False, "percent": 0.0},
    "time_stop": {"enabled": False, "max_bars": 0},
}


def _build(strategy_id, name, description, symbol, **guards):
    """买入持有 + 单个风控器。guards 覆盖 DISABLED_GUARDS 里的某一项。"""
    protection_params = dict(DISABLED_GUARDS)
    protection_params.update(guards)

    return (StrategyBuilder(
                strategy_id, name,
                description=description,
                backtest=dict(BACKTEST))
            .input("1", symbol=symbol)
            .signal("2", symbol, buy="true", sell="false")
            .portfolio("3", position_ratio=1.0)
            .add(protection_node("5", **protection_params))
            .execution("4")
            .connect("1", "2")
            .connect("2", "3")
            .connect("3", "5")
            .connect("5", "4")
            .build())


# 每个夹具：文件名 → (策略 id, 名称, 描述, 标的, 风控器参数)
FIXTURES = {
    "stop_loss_protection_strategy.json": (
        "test_stop_loss_protection", "风控测试_止损",
        "买入持有 + 5%止损保护（用于 ProtectionNode 验证）",
        DOWN_SYMBOL,
        {"stop_loss": {"enabled": True, "percent": 0.05}},
    ),
    "take_profit_protection_strategy.json": (
        "test_take_profit_protection", "风控测试_止盈",
        "买入持有 + 5%止盈保护（用于 ProtectionNode 验证）",
        UP_SYMBOL,
        {"take_profit": {"enabled": True, "percent": 0.05}},
    ),
    "atr_stop_loss_strategy.json": (
        "test_atr_stop_loss", "风控测试_ATR止损",
        "买入持有 + ATR自适应止损（用于 ProtectionNode Phase 0 验证）",
        DOWN_SYMBOL,
        {"atr_stop_loss": {"enabled": True, "period": 20, "multiplier": 2.0}},
    ),
    "ma_stop_loss_strategy.json": (
        "test_ma_stop_loss", "风控测试_MA止损",
        "买入持有 + MA跌破止损（用于 ProtectionNode Phase 0 验证）",
        DOWN_SYMBOL,
        {"ma_stop_loss": {"enabled": True, "period": 20}},
    ),
    "r2_stop_loss_strategy.json": (
        "test_r2_stop_loss", "风控测试_R2止损",
        "买入持有 + R²趋势消失止损（用于 ProtectionNode Phase 0 验证）",
        DOWN_SYMBOL,
        {"r2_stop_loss": {"enabled": True, "period": 10, "threshold": 0.5}},
    ),
    "mae_stop_loss_strategy.json": (
        "test_mae_stop_loss", "风控测试_MAE止损",
        "买入持有 + MAE最大不利偏移止损（用于 ProtectionNode Phase 0 验证）",
        DOWN_SYMBOL,
        {"mae_stop_loss": {"enabled": True, "percent": 0.05}},
    ),
}


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for filename, (strategy_id, name, description, symbol, guards) in FIXTURES.items():
        strategy = _build(strategy_id, name, description, symbol, **guards)
        path = OUT_DIR / filename
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(strategy, handle, indent=2, ensure_ascii=False)
        print(f"  策略: {path}")
    print(f"共 {len(FIXTURES)} 个风控夹具")


if __name__ == "__main__":
    main()
