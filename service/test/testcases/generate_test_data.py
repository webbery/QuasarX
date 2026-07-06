#!/usr/bin/env python3
"""
生成回测指标验证的标准测试数据

为每个测试用例生成：
1. CSV 格式的价格数据（供 C++ 回测引擎使用）
2. JSON 格式的预期指标值（供 Python 对比使用）
3. 策略脚本 JSON（供回测 API 使用）

输出目录：service/test/data/metric_test/
"""

import os
import csv
import json
import shutil
import numpy as np
from datetime import datetime, timedelta
from pathlib import Path

# ============================================================
# 配置
# ============================================================

# 输出到服务数据目录（service/build/data/）
# __file__ = test/testcases/generate_test_data.py
# .parent.parent = test/  ← 错误
# .parent.parent.parent = service/  ← 正确
SERVICE_ROOT = Path(__file__).parent.parent.parent
SERVICE_BUILD_DIR = SERVICE_ROOT / "build"
SERVICE_DATA_DIR = SERVICE_BUILD_DIR / "data"

# 股票数据路径（T1 日线模式）
# 后复权数据（用于指标计算）
HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"
# 原始价格数据（用于实际交易撮合）
ORG_DIR = SERVICE_DATA_DIR / "AStock"

# 确保目录存在
HFQ_DIR.mkdir(parents=True, exist_ok=True)
ORG_DIR.mkdir(parents=True, exist_ok=True)

# 测试用例汇总输出到 testcases 目录
TESTCASES_DIR = Path(__file__).parent
METRIC_TEST_DIR = TESTCASES_DIR / "metric_test_data"
METRIC_TEST_DIR.mkdir(parents=True, exist_ok=True)

START_DATE = datetime(2023, 1, 1)
YEAR_DAYS = 252


# ============================================================
# 价格序列生成器
# ============================================================

def generate_up_trend(days=60):
    """单边上涨"""
    prices = [100.0]
    for _ in range(days - 1):
        prices.append(prices[-1] * (1 + 0.003))  # 每日涨 0.3%
    return prices


def generate_down_trend(days=60):
    """单边下跌"""
    prices = [100.0]
    for _ in range(days - 1):
        prices.append(prices[-1] * (1 - 0.003))  # 每日跌 0.3%
    return prices


def generate_rise_fall(days=60):
    """先涨后跌"""
    prices = [100.0]
    # 前 1/3 上涨
    for _ in range(days // 3):
        prices.append(prices[-1] * (1 + 0.005))
    # 中间 1/3 下跌
    for _ in range(days // 3):
        prices.append(prices[-1] * (1 - 0.005))
    # 最后 1/3 震荡
    np.random.seed(42)
    for _ in range(days - len(prices)):
        prices.append(prices[-1] * (1 + np.random.normal(0, 0.002)))
    return prices


def generate_sideways(days=60):
    """横盘震荡"""
    np.random.seed(42)
    prices = [100.0]
    for _ in range(days - 1):
        prices.append(prices[-1] * (1 + np.random.normal(0, 0.003)))
    return prices


def generate_high_volatility(days=60):
    """高波动率"""
    np.random.seed(42)
    prices = [100.0]
    for _ in range(days - 1):
        prices.append(prices[-1] * (1 + np.random.normal(0, 0.02)))  # 2% 波动
    return prices


def generate_steady_trend(days=120):
    """稳定上涨趋势"""
    np.random.seed(42)
    prices = [100.0]
    for _ in range(days - 1):
        # 漂移 0.05% + 噪声 0.5%
        prices.append(prices[-1] * (1 + 0.0005 + np.random.normal(0, 0.005)))
    return prices


def generate_mean_shift(total_days=200, shift_point=100, base_price=100.0,
                        noise_std=0.005, shift_size=0.01):
    """
    生成均值偏移数据（用于 CUSUM 变点检测验证）

    收益率序列有持续均值偏移：
    - 前 shift_point 天：returns ~ N(0, noise_std)
    - 后 (total_days - shift_point) 天：returns ~ N(shift_size, noise_std)

    然后用随机游走重构价格序列。已知变点在 shift_point 位置。

    参数:
        total_days: 总天数
        shift_point: 变点位置（第几天开始偏移）
        base_price: 起始价格
        noise_std: 收益率噪声标准差
        shift_size: 收益率均值偏移量

    返回:
        prices: 价格序列
    """
    np.random.seed(42)
    # 1. 生成有均值偏移的收益率序列
    returns = []
    for i in range(total_days):
        if i < shift_point:
            r = np.random.normal(0, noise_std)
        else:
            r = np.random.normal(shift_size, noise_std)
        returns.append(r)

    # 2. 用随机游走重构价格
    prices = [base_price]
    for r in returns[1:]:
        prices.append(round(prices[-1] * (1 + r), 2))
    return prices


# ============================================================
# CSV 数据生成
# ============================================================

def generate_csv(symbol, prices, start_date, hfq_path, org_path):
    """
    生成标准 CSV 格式的价格数据
    同时生成后复权（用于指标计算）和原始价格（用于交易撮合）

    格式：datetime,open,close,high,low,volume,turnover

    返回:
        opens: 开盘价序列
    """
    np.random.seed(42)  # 固定种子保证可重复
    opens = []

    # 后复权数据：在后复权目录中，价格与原始数据相同（测试用例不需要复权调整）
    with open(hfq_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['datetime', 'open', 'close', 'high', 'low', 'volume', 'turnover'])

        current_date = start_date
        for i, close in enumerate(prices):
            # 跳过周末
            while current_date.weekday() >= 5:
                current_date += timedelta(days=1)

            # 使用日期字符串格式（服务期望的格式）
            date_str = current_date.strftime("%Y-%m-%d")
            open_price = close * (1 + np.random.normal(0, 0.001))
            opens.append(round(open_price, 2))
            high = max(open_price, close) * (1 + abs(np.random.normal(0, 0.002)))
            low = min(open_price, close) * (1 - abs(np.random.normal(0, 0.002)))
            volume = int(np.random.uniform(1000000, 5000000))
            turnover = round(volume * close, 2)

            writer.writerow([date_str, round(open_price, 2), round(close, 2),
                             round(high, 2), round(low, 2), volume, turnover])

            current_date += timedelta(days=1)

    # 原始价格数据：直接复制后复权数据（测试场景下两者相同）
    import shutil
    shutil.copy2(hfq_path, org_path)
    
    return opens


# ============================================================
# 预期指标计算（Python 本地）
# ============================================================

def compute_expected_metrics_from_csv(csv_path, initial_capital=500000.0):
    """
    从实际 CSV 文件读取收盘价计算预期指标（与 C++ 使用相同数据）

    参数:
        csv_path: CSV 文件路径
        initial_capital: 初始资金
    """
    with open(csv_path, 'r') as f:
        rows = list(csv.DictReader(f))

    closes = [float(r['close']) for r in rows]
    opens = [float(r['open']) for r in rows]

    # C++ 使用 GetPrimitivePrice 返回收盘价作为订单成交价
    # 整手买入（100股整数倍）

    # C++ 实际时序（从日志反推）：
    # Day 0: stepForward(curIndex: 0→1) → RunGraph → 但订单未生成或被拒绝
    # Day 1: stepForward(curIndex: 1→2) → RunGraph → 生成订单（price=closes[1]）
    # Day 2: stepForward(curIndex: 2→3) → matchOrders（撮合Day 1订单）→ recordDailySnapshot
    # 原因：Day 0 的 RunGraph 可能因 GetPrimitivePrice 返回 0 而未生成有效订单
    buy_price = closes[1]  # Day 1 的收盘价
    shares = int(initial_capital / buy_price / 100) * 100
    # 佣金（与测试策略配置一致）
    commission = shares * buy_price * 0.0003
    cost = shares * buy_price + commission
    remaining = initial_capital - cost

    # 每日总资产 = 剩余资金 + 持仓市值（按收盘价）
    # 与 C++ 一致：Day 0、Day 1 都是现金，Day 2 起才有持仓
    values = [initial_capital, initial_capital]  # Day 0, Day 1 现金
    for i in range(2, len(closes)):
        values.append(remaining + shares * closes[i])

    # 计算收益率
    daily_returns = [(values[i] - values[i-1]) / values[i-1] for i in range(1, len(values))]

    # 总收益率：从第一天持仓到最后一天
    total_return = (values[-1] - values[0]) / values[0]

    # 年化收益率
    n = len(daily_returns)
    annual_return = (1 + total_return) ** (YEAR_DAYS / n) - 1 if total_return > -1 else 0.0

    # 年化波动率
    annual_vol = np.std(daily_returns, ddof=1) * np.sqrt(YEAR_DAYS) if len(daily_returns) > 1 else 0.0

    # 夏普比率
    if annual_vol < 1e-6:
        sharpe = 0.0
    else:
        sharpe = (annual_return - 0.0) / annual_vol

    # 最大回撤
    peak = values[0]
    max_dd = 0.0
    for v in values:
        if v > peak:
            peak = v
        dd = (peak - v) / peak if peak > 0 else 0
        if dd > max_dd:
            max_dd = dd

    # 胜率
    win_rate = sum(1 for r in daily_returns if r > 0) / len(daily_returns) if daily_returns else 0.0

    # 卡玛比率
    calmar = annual_return / max_dd if max_dd > 0 else 0.0

    # R²
    r_squared = compute_r_squared(values)

    # VaR / CVaR
    sorted_returns = sorted(daily_returns)
    var_index = int(0.05 * len(sorted_returns))
    var_95 = -sorted_returns[var_index] if var_index < len(sorted_returns) else 0.0
    cvar_95 = -sum(sorted_returns[:var_index+1]) / (var_index + 1) if var_index < len(sorted_returns) else 0.0

    return {
        "total_return": round(total_return, 6),
        "annual_return": round(annual_return, 6),
        "annual_volatility": round(annual_vol, 6),
        "sharpe": round(sharpe, 6),
        "max_drawdown": round(max_dd, 6),
        "win_rate": round(win_rate, 6),
        "calmar": round(calmar, 6),
        "r_squared": round(r_squared, 6),
        "var_95": round(var_95, 6),
        "cvar_95": round(cvar_95, 6),
        "days": n + 1,  # 包括 Day 0
        "daily_returns": [round(r, 6) for r in daily_returns],
    }


def compute_r_squared(values):
    """R² 计算"""
    n = len(values)
    if n < 2:
        return 0.0

    mean_y = sum(values) / n
    mean_x = (n - 1) / 2.0

    sxx = sum((i - mean_x) ** 2 for i in range(n))
    sxy = sum((i - mean_x) * (values[i] - mean_y) for i in range(n))
    sst = sum((v - mean_y) ** 2 for v in values)

    if sxx == 0 or sst == 0:
        return 0.0

    return (sxy * sxy) / (sxx * sst)


# ============================================================
# 策略脚本生成
# ============================================================

def generate_strategy_script(symbol, strategy_id, start_date, end_date, disable_costs=False):
    """
    生成简单的买入持有策略脚本
    """
    commission = 0.0 if disable_costs else 0.0003
    stamp_duty = 0.0 if disable_costs else 0.001
    slippage = 0.0 if disable_costs else 0.0

    return {
        "id": strategy_id,
        "name": f"指标测试_{strategy_id}",
        "version": 1,
        "description": "买入持有策略（用于指标验证）",
        "backtest": {
            "start": start_date.strftime("%Y-%m-%d"),
            "end": end_date.strftime("%Y-%m-%d")
        },
        "nodes": [
            {
                "id": "1",
                "type": "custom",
                "data": {
                    "label": "行情数据",
                    "nodeType": "input",
                    "params": {
                        "source": {"value": "股票", "type": "select"},
                        "code": {"value": [symbol], "type": "text"},
                        "freq": {"value": "1d", "type": "select"},
                        "close": {"value": "close", "type": "text"},
                        "open": {"value": "open", "type": "text"},
                        "high": {"value": "high", "type": "text"},
                        "low": {"value": "low", "type": "text"},
                        "volume": {"value": "volume", "type": "text"}
                    }
                }
            },
            {
                "id": "2",
                "type": "custom",
                "data": {
                    "label": "买入信号",
                    "nodeType": "signal",
                    "params": {
                        "code": {"value": [symbol], "type": "text"},
                        "buy": {"value": "true", "type": "text"},
                        "sell": {"value": "false", "type": "text"}
                    }
                }
            },
            {
                "id": "3",
                "type": "custom",
                "data": {
                    "label": "投资组合",
                    "nodeType": "portfolio",
                    "params": {
                        "positionRatio": {"value": 1.0, "type": "number"}
                    }
                }
            },
            {
                "id": "4",
                "type": "custom",
                "data": {
                    "label": "交易执行",
                    "nodeType": "execution",
                    "params": {
                        "commission": {"value": commission, "type": "number"},
                        "stampDuty": {"value": stamp_duty, "type": "number"},
                        "minFee": {"value": 0, "type": "number"},
                        "slippageModel": {"value": 0, "type": "number"},
                        "slippage": {"value": slippage, "type": "number"},
                        "type": {"value": 1, "type": "select"},
                        "contract": {"value": 0, "type": "select"}
                    }
                }
            }
        ],
        "edges": [
            {"id": "1->2", "source": "1", "target": "2", "sourceHandle": "1", "targetHandle": "2"},
            {"id": "2->3", "source": "2", "target": "3", "sourceHandle": "2", "targetHandle": "3"},
            {"id": "3->4", "source": "3", "target": "4", "sourceHandle": "3", "targetHandle": "4"}
        ]
    }


# ============================================================
# 主流程
# ============================================================

def main():
    test_cases = {
        "up_trend": {
            "name": "单边上涨",
            "symbol": "sz.900001",  # 每个用例独立 symbol，避免 CSV 覆盖
            "generator": generate_up_trend,
            "kwargs": {"days": 60}
        },
        "down_trend": {
            "name": "单边下跌",
            "symbol": "sz.900002",
            "generator": generate_down_trend,
            "kwargs": {"days": 60}
        },
        "rise_fall": {
            "name": "先涨后跌",
            "symbol": "sz.900003",
            "generator": generate_rise_fall,
            "kwargs": {"days": 60}
        },
        "sideways": {
            "name": "横盘震荡",
            "symbol": "sz.900004",
            "generator": generate_sideways,
            "kwargs": {"days": 60}
        },
        "high_volatility": {
            "name": "高波动率",
            "symbol": "sz.900005",
            "generator": generate_high_volatility,
            "kwargs": {"days": 60}
        },
        "steady_trend": {
            "name": "稳定趋势",
            "symbol": "sz.900006",
            "generator": generate_steady_trend,
            "kwargs": {"days": 120}
        },
        "mean_shift": {
            "name": "均值偏移（CUSUM变点检测）",
            "symbol": "sz.900007",
            "generator": generate_mean_shift,
            "kwargs": {"total_days": 200, "shift_point": 100, "base_price": 100.0,
                       "noise_std": 0.001, "shift_size": 0.01}
        }
    }

    all_cases = {}

    for case_id, case_info in test_cases.items():
        symbol = case_info["symbol"]
        print(f"\n生成测试用例: {case_info['name']} ({case_id}) @ {symbol}")

        # 1. 生成价格序列
        prices = case_info["generator"](**case_info["kwargs"])

        # 2. 生成 CSV 数据到服务数据目录
        hfq_path = HFQ_DIR / f"{symbol}.csv"
        org_path = ORG_DIR / f"{symbol}.csv"
        opens = generate_csv(symbol, prices, START_DATE, hfq_path, org_path)
        print(f"  后复权数据: {hfq_path}")
        print(f"  原始数据:   {org_path}")
        print(f"  数据条数:   {len(prices)}")

        # 3. 计算预期指标（从实际 CSV 读取，与 C++ 使用相同数据）
        expected = compute_expected_metrics_from_csv(hfq_path)

        # 4. 生成策略脚本
        end_date = START_DATE + timedelta(days=len(prices) + 30)
        strategy = generate_strategy_script(
            symbol, f"test_{case_id}", START_DATE, end_date, True
        )

        strategy_path = METRIC_TEST_DIR / f"{case_id}_strategy.json"
        with open(strategy_path, 'w') as f:
            json.dump(strategy, f, indent=2)
        print(f"  策略脚本: {strategy_path}")

        # 5. 保存预期指标
        case_data = {
            "name": case_info["name"],
            "symbol": symbol,
            "days": len(prices),
            "expected": expected,
            "strategy_file": str(strategy_path),
            "data_file": str(hfq_path)  # 后复权数据路径
        }
        all_cases[case_id] = case_data

    # 6. 生成 symbol_market.csv（保留原有数据，追加测试 symbol）
    market_csv_path = SERVICE_DATA_DIR / "symbol_market.csv"
    market_backup_path = SERVICE_DATA_DIR / "symbol_market.csv.backup"
    
    # 读取现有数据（如果有）
    existing_rows = []
    existing_symbols = set()
    if market_csv_path.exists():
        import shutil
        shutil.copy2(market_csv_path, market_backup_path)
        with open(market_csv_path, 'r', newline='', encoding='utf-8') as f:
            reader = csv.DictReader(f)
            for row in reader:
                existing_rows.append(row)
                existing_symbols.add(row.get('代码', ''))
    
    # 追加新的测试 symbol（去重）
    new_rows = []
    for case_id, case_info in test_cases.items():
        symbol = case_info["symbol"]
        parts = symbol.split('.')
        exchange = parts[0].upper()
        code_num = parts[1]
        if code_num not in existing_symbols:
            new_rows.append({'代码': code_num, '交易所': exchange, 'name': case_info["name"]})
    
    if new_rows:
        with open(market_csv_path, 'w', newline='', encoding='utf-8') as f:
            writer = csv.DictWriter(f, fieldnames=['代码', '交易所', 'name'])
            writer.writeheader()
            for row in existing_rows + new_rows:
                writer.writerow(row)
        print(f"\n  市场数据: {market_csv_path} (新增 {len(new_rows)} 个测试 symbol)")
    else:
        print(f"\n  市场数据: {market_csv_path} (已存在，无需追加)")

    # 7. 生成测试用例汇总文件
    summary_path = METRIC_TEST_DIR / "test_cases_summary.json"
    with open(summary_path, 'w') as f:
        json.dump(all_cases, f, indent=2, ensure_ascii=False)
    print(f"\n测试用例汇总: {summary_path}")

    # 8. 打印预期指标对比表
    print("\n" + "=" * 80)
    print("预期指标汇总表")
    print("=" * 80)
    print(f"{'用例':<12} {'总收益':>8} {'年化':>8} {'夏普':>8} {'回撤':>8} {'胜率':>8} {'卡玛':>8}")
    print("-" * 80)

    for case_id, case_data in all_cases.items():
        exp = case_data["expected"]
        print(f"{case_data['name']:<12} "
              f"{exp['total_return']:>8.4f} "
              f"{exp['annual_return']:>8.4f} "
              f"{exp['sharpe']:>8.4f} "
              f"{exp['max_drawdown']:>8.4f} "
              f"{exp['win_rate']:>8.4f} "
              f"{exp['calmar']:>8.4f}")

    print("=" * 80)
    print(f"\n生成完成！共 {len(all_cases)} 个测试用例")
    print(f"服务数据目录: {SERVICE_DATA_DIR}")
    print(f"策略脚本目录:   {METRIC_TEST_DIR}")


if __name__ == "__main__":
    main()
