#!/usr/bin/env python3
"""
生成两融测试策略

生成 node_test_data/margin_test_strategy.json，用于 test_credit.py 测试。

策略逻辑：
- 输入：sz.900001 的日线数据
- 信号：买入条件为 close > 100（恒真，确保触发买入）
- 信号：卖出条件为 close < 50（恒假，不触发卖出）
- 配置：margin.allowShort = true, marginRatio = 0.5
- 配置：financingRate = 0.08, securitiesLendingRate = 0.10
"""

import json
from pathlib import Path
import sys

# 添加 testcases 目录到 path
sys.path.insert(0, str(Path(__file__).resolve().parent.parent))

# 直接导入 strategy_builder（避免导入 tool.py 依赖 pytest）
from strategy_builder import StrategyBuilder


def generate_margin_test_strategy():
    """生成两融测试策略"""
    builder = StrategyBuilder(
        strategy_id="margin_test_strategy",
        name="两融测试策略",
        version=1,
        description="用于测试两融功能：融资买入、融券卖出、两融费用计算",
        capital=200000,
        backtest=("2024-01-01", "2024-12-31"),
        source="A_hfq",
    )
    
    # 构建基本策略
    strategy = (
        builder
        .input("1", symbol="sz.900001")
        .signal("2", symbol="sz.900001", 
                buy="close[t] > 100",
                sell="close[t] < 50",
                allow_short=True)
        .portfolio("3", position_ratio=1.0, allow_short=True)
        .execution("4", commission=0.0003, stamp_duty=0.001, slippage=0.0005)
        .connect("1", "2", prop="close")
        .connect("2", "3")
        .connect("3", "4")
        .build()
    )
    
    # 手动添加 margin 配置（StrategyBuilder 不直接支持）
    strategy["margin"] = {
        "allowShort": True,
        "marginRatio": 0.5,  # 50% 保证金，触发融资
        "maintenanceMarginRatio": 0.3,
        "financingRate": 0.08,  # 融资年利率 8%
        "securitiesLendingRate": 0.10,  # 融券年费率 10%
        "warningRatio": 1.5,  # 警戒线 150%
        "liquidationRatio": 1.3,  # 平仓线 130%
    }
    
    return strategy


if __name__ == "__main__":
    output_path = Path(__file__).parent / "margin_test_strategy.json"
    strategy = generate_margin_test_strategy()
    
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(strategy, f, indent=2, ensure_ascii=False)
    
    print(f"已生成测试策略: {output_path}")
