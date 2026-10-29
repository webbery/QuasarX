#!/usr/bin/env python3
"""
生成节点测试的标准数据

输出：
- CSV: service/build/data/A_hfq/ 和 AStock/
- 策略 JSON: testcases/node_test_data/
- 汇总: test_data_summary.json

使用方法：
  cd service/test/testcases/node_test_data
  python generate_node_data.py
"""

import csv
import json
import math
import shutil
import sys
import numpy as np
from datetime import datetime, timedelta
from pathlib import Path

# 路径统一从 tool.py 解析
sys.path.insert(0, str(Path(__file__).parent.parent))
from tool import _DATA_DIR as SERVICE_DATA_DIR

# 策略图构造统一走 strategy_builder（节点/边/参数结构的唯一事实来源）
from strategy_builder import (StrategyBuilder, input_node, function_node,
                              formula_node, debug_node, signal_node,
                              portfolio_node, execution_node, cusum_node,
                              emd_node, pair_trading_strategy)

HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"
ORG_DIR = SERVICE_DATA_DIR / "AStock"
HFQ_DIR.mkdir(parents=True, exist_ok=True)
ORG_DIR.mkdir(parents=True, exist_ok=True)

TEST_DATA_DIR = Path(__file__).parent
START_DATE = datetime(2024, 1, 1)


def gen_sine(n_bars=200, amplitude=10.0, period=40, noise_std=0.5, base=100.0, seed=42):
    np.random.seed(seed)
    t = np.arange(n_bars)
    noise = np.random.normal(0, noise_std, n_bars)
    signal = base + amplitude * np.sin(2 * np.pi * t / period) + noise
    return [round(float(p), 2) for p in signal]


def gen_step(n_bars=200, base=100.0, jump_at=100, jump_size=10.0, noise_std=0.5, seed=42):
    np.random.seed(seed)
    prices = [base]
    for i in range(1, n_bars):
        target = base if i < jump_at else base + jump_size
        ret = (target - prices[-1]) * 0.1 + np.random.normal(0, noise_std)
        prices.append(round(prices[-1] * (1 + ret / 100), 2))
    return prices


def gen_reversal(n_bars=200, seed=42):
    np.random.seed(seed)
    prices = [100.0]
    segments = [(50, 0.003), (50, -0.003), (50, 0.0), (50, 0.003)]
    for n_seg, drift in segments:
        for _ in range(n_seg):
            ret = (drift + np.random.normal(0, 0.001)) if drift else np.random.normal(0, 0.002)
            prices.append(round(prices[-1] * (1 + ret), 2))
    return prices


def gen_deterministic(n_bars=200):
    return [float(100 + i) for i in range(n_bars)]


def gen_anomaly(n_bars=200, anomaly_bar=100, seed=42):
    np.random.seed(seed)
    prices = [100.0]
    for i in range(1, n_bars):
        if i == anomaly_bar:
            prices.append(0.0)
        else:
            ret = np.random.normal(0.0005, 0.01)
            prices.append(round(prices[-1] * (1 + ret), 2))
    return prices


# ───────────────────────── 配对交易数据集 ─────────────────────────
#
# 分子腿 A 走带噪声的随机游走；分母腿 B = A / β · (1 + ε)，ε 是小噪声。
# 于是价差 s_t = A_t - (α + β·B_t) 在 0 附近平稳，β 的滚动 OLS 估计也稳定在真值附近。
#
# 中段给 A 加一个温和上冲（外生偏离）：z 越过 entryZ 触发入场，随后 A 回到基准
# 路径、z 回到 exitZ 以内触发出场。偏离幅度控制在 stopZ 以内，否则入场当根就被
# 止损打掉。
#
# 两只标的必须在 margin_stock 名单里（prepare.py 预灌），否则空头腿会被
# 做空门控降级为 HOLD，配对仓位根本建不起来。
PAIR_SYMBOL_A = "sz.900101"   # 分子腿（价差 = P_A - β·P_B 的 A）
PAIR_SYMBOL_B = "sz.900102"   # 分母腿
PAIR_N_BARS = 200
PAIR_TRUE_BETA = 1.25         # A 对 B 的真实对冲系数
PAIR_A_BASE = 100.0
PAIR_A_VOL = 0.004            # A 的日波动
PAIR_EPS_VOL = 0.0015         # B 相对 A/β 的噪声
PAIR_EXC_START = 80           # 偏离起始 bar
PAIR_EXC_RAMP_UP = 4          # 上冲段长度
PAIR_EXC_HOLD = 16            # 平台段长度
PAIR_EXC_RAMP_DOWN = 6        # 回落段长度
PAIR_EXC_AMPLITUDE = 0.0026   # 平台偏离幅度（相对 A 的比例）
PAIR_SEED = 20261029

PAIR_BETA_WINDOW = 30         # 价差节点 window
PAIR_Z_WINDOW = 40            # ZScore 窗口
PAIR_ENTRY_Z = 2.0
PAIR_EXIT_Z = 0.5
PAIR_STOP_Z = 3.5


def gen_pair_legs(n_bars=PAIR_N_BARS, seed=PAIR_SEED):
    """生成配对的两条腿收盘价序列 (A, B)。

    关键点：偏离只加在 A 上，B 走基准路径。若两边一起动，A = α + β·B 的关系
    根本没变，价差不动 —— 那样就构造不出可触发的偏离。
    """
    rng = np.random.default_rng(seed)

    # 基准路径：A 走带噪声的随机游走
    rets = rng.normal(0.0, PAIR_A_VOL, n_bars)
    base = PAIR_A_BASE * np.cumprod(1.0 + rets)

    # 偏离段（梯形：上冲 → 平台 → 回落），只作用在 A 上
    peak_start = PAIR_EXC_START + PAIR_EXC_RAMP_UP
    peak_end = peak_start + PAIR_EXC_HOLD
    exc_end = peak_end + PAIR_EXC_RAMP_DOWN
    excursion = np.zeros(n_bars)
    for t in range(PAIR_EXC_START, exc_end):
        if t < peak_start:
            ramp = (t - PAIR_EXC_START) / float(PAIR_EXC_RAMP_UP)
        elif t < peak_end:
            ramp = 1.0
        else:
            ramp = (exc_end - t) / float(PAIR_EXC_RAMP_DOWN)
        excursion[t] = PAIR_EXC_AMPLITUDE * ramp
    a = base * (1.0 + excursion)

    # B = 基准路径 / β · (1 + ε)
    eps = rng.normal(0.0, PAIR_EPS_VOL, n_bars)
    b = base / PAIR_TRUE_BETA * (1.0 + eps)

    return ([round(float(x), 4) for x in a], [round(float(x), 4) for x in b])


def ols(xs, ys):
    """最小二乘：ys = alpha + beta * xs"""
    n = len(xs)
    mean_x = sum(xs) / n
    mean_y = sum(ys) / n
    cov = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
    var = sum((x - mean_x) ** 2 for x in xs)
    if var < 1e-12:
        raise ValueError("B 价无波动，无法回归")
    beta = cov / var
    return beta, mean_y - beta * mean_x


def reference_spread(a, b, window, include_current=False):
    """复刻 SpreadNode 的价差。

    β 用 [t-window, t-1] 的完整窗口估计，再外推到 t（include_current=False，
    与服务端一致）。include_current=True 时把当根也放进回归窗口 —— 那是
    in-sample 口径，会把当根残差拉回，测试用它来证明服务端确实做了滞后。
    窗口未满返回 NaN。
    """
    spreads, betas = [], []
    for t in range(len(a)):
        lo = t - window
        hi = t + 1 if include_current else t
        if lo < 0:
            spreads.append(float("nan"))
            betas.append(float("nan"))
            continue
        beta, alpha = ols(b[lo:hi], a[lo:hi])
        spreads.append(a[t] - (alpha + beta * b[t]))
        betas.append(beta)
    return spreads, betas


def reference_zscore(spreads, window):
    """复刻 FunctionNode(ZScore)：总体标准差(ddof=0)、含当根、遇 NaN 跳过。

    窗口未满返回 NaN。
    """
    buf = []
    out = []
    for s in spreads:
        if math.isnan(s):
            out.append(float("nan"))
            continue
        buf.append(s)
        if len(buf) > window:
            buf.pop(0)
        if len(buf) < window:
            out.append(float("nan"))
            continue
        mean = sum(buf) / len(buf)
        var = sum((x - mean) ** 2 for x in buf) / len(buf)
        out.append((s - mean) / math.sqrt(var) if var > 0 else 0.0)
    return out


def pair_spread_strategy():
    """价差节点测试：Input(A) + Input(B) → Spread → Debug"""
    return (StrategyBuilder("pair_spread", "配对价差测试",
                             description="SpreadNode 数值校验",
                             backtest={"start": "2024-01-01", "end": "2024-12-31"},
                             source="A_hfq")
            .input("1", symbol=PAIR_SYMBOL_A)
            .input("2", symbol=PAIR_SYMBOL_B)
            .spread("3", method="rolling_regression", window=PAIR_BETA_WINDOW,
                    symbol_a=PAIR_SYMBOL_A)
            .debug("4", "debug_pair_spread")
            .connect("1", "3", prop="close")
            .connect("2", "3", prop="close")
            .connect("3", "4")
            .build())


def generate_pair_dataset():
    """写配对数据集的 CSV、策略 JSON 与参考序列。"""
    a, b = gen_pair_legs()
    spreads, betas = reference_spread(a, b, PAIR_BETA_WINDOW)
    insample_spreads, _ = reference_spread(a, b, PAIR_BETA_WINDOW,
                                            include_current=True)
    zs = reference_zscore(spreads, PAIR_Z_WINDOW)

    write_csv(PAIR_SYMBOL_A, a, START_DATE, HFQ_DIR / f"{PAIR_SYMBOL_A}.csv",
              ORG_DIR / f"{PAIR_SYMBOL_A}.csv")
    write_csv(PAIR_SYMBOL_B, b, START_DATE, HFQ_DIR / f"{PAIR_SYMBOL_B}.csv",
              ORG_DIR / f"{PAIR_SYMBOL_B}.csv")
    print(f"  CSV: {PAIR_SYMBOL_A} / {PAIR_SYMBOL_B} 各 {len(a)} bars")

    spread_strategy = pair_spread_strategy()
    with open(TEST_DATA_DIR / "pair_spread.json", 'w') as f:
        json.dump(spread_strategy, f, indent=2, ensure_ascii=False)

    pair_strategy = pair_trading_strategy(
        "pair_trading", PAIR_SYMBOL_A, PAIR_SYMBOL_B,
        name="配对交易测试",
        description="两腿偏离阈值入场、回归出场",
        capital=200000,
        backtest={"start": "2024-01-01", "end": "2024-12-31"},
        source="A_hfq",
        method="rolling_regression", window=PAIR_BETA_WINDOW,
        z_window=PAIR_Z_WINDOW, entry_z=PAIR_ENTRY_Z, exit_z=PAIR_EXIT_Z,
        stop_z=PAIR_STOP_Z, allow_short=True, sizing_method="strength",
        debug_label="debug_pair")
    with open(TEST_DATA_DIR / "pair_trading.json", 'w') as f:
        json.dump(pair_strategy, f, indent=2, ensure_ascii=False)

    valid = [(i, z) for i, z in enumerate(zs) if not math.isnan(z)]
    entries = [i for i, z in valid if abs(z) >= PAIR_ENTRY_Z]
    print(f"  z 有效区间: bar {valid[0][0]} .. {valid[-1][0]}")
    print(f"  z 极值: {max(valid, key=lambda x: abs(x[1]))}")
    print(f"  |z| >= {PAIR_ENTRY_Z} 的 bar: {entries}（共 {len(entries)} 根）")

    return {
        "symbol_a": PAIR_SYMBOL_A,
        "symbol_b": PAIR_SYMBOL_B,
        "n_bars": len(a),
        "true_beta": PAIR_TRUE_BETA,
        "beta_window": PAIR_BETA_WINDOW,
        "z_window": PAIR_Z_WINDOW,
        "entry_z": PAIR_ENTRY_Z,
        "exit_z": PAIR_EXIT_Z,
        "stop_z": PAIR_STOP_Z,
        "excursion": {"start": PAIR_EXC_START,
                      "ramp_up": PAIR_EXC_RAMP_UP, "hold": PAIR_EXC_HOLD,
                      "ramp_down": PAIR_EXC_RAMP_DOWN,
                      "amplitude": PAIR_EXC_AMPLITUDE},
        "entry_bars": entries,
        "reference": {
            "spread": spreads,
            "beta": betas,
            "z": zs,
            "insample_spread": insample_spreads,
        },
        "strategies": {"spread": "pair_spread", "pair": "pair_trading"},
    }


def write_csv(symbol, prices, start_date, hfq_path, org_path, anomaly_bar=None):
    np.random.seed(42)
    dates = []
    rows_data = []
    current_date = start_date
    for i, close in enumerate(prices):
        while current_date.weekday() >= 5:
            current_date += timedelta(days=1)
        date_str = current_date.strftime("%Y-%m-%d")
        dates.append(date_str)

        if close <= 0:
            open_price = high = low = 0.0
        else:
            open_price = close * (1 + np.random.normal(0, 0.001))
            high = max(open_price, close) * (1 + abs(np.random.normal(0, 0.002)))
            low = min(open_price, close) * (1 - abs(np.random.normal(0, 0.002)))

        volume = 0 if (anomaly_bar is not None and i == anomaly_bar + 1) else int(np.random.uniform(1000000, 5000000))
        turnover = round(volume * close, 2) if close > 0 else 0.0

        rows_data.append([date_str, round(open_price, 2), round(close, 2),
                          round(high, 2), round(low, 2), volume, turnover])
        current_date += timedelta(days=1)

    # 写 hfq (后复权，连续价格)
    with open(hfq_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['datetime', 'open', 'close', 'high', 'low', 'volume', 'turnover'])
        for row in rows_data:
            writer.writerow(row)
    # 默认 org = hfq（无分红时两者一致）
    shutil.copy2(hfq_path, org_path)
    return dates


def generate_dividend_data(symbol, dates, hfq_path, org_path,
                           ex_div_bar=100, cash_per_10=5.0):
    """为指定标的生成分红测试数据

    1. 修改 org CSV：除权日及之后价格下调 cash_per_10/10
    2. 生成 dividend CSV：导入 FinanceDB 供回测引擎处理

    hfq 保持连续（信号计算不受影响），org 含除权跌价（订单执行用）。
    """
    cash_per_share = cash_per_10 / 10.0

    # 读取 hfq CSV，修改 org 价格
    rows = []
    with open(hfq_path, 'r') as f:
        reader = csv.reader(f)
        header = next(reader)
        for row in reader:
            rows.append(row)

    with open(org_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(header)
        for i, row in enumerate(rows):
            if i >= ex_div_bar:
                # 除权日及之后: 价格下调
                row = list(row)
                for col_idx in [1, 2, 3, 4]:  # open, close, high, low
                    row[col_idx] = round(float(row[col_idx]) - cash_per_share, 2)
            writer.writerow(row)

    # 生成 dividend CSV
    div_dir = SERVICE_DATA_DIR / "dividend"
    div_dir.mkdir(parents=True, exist_ok=True)
    div_path = div_dir / f"{symbol}_dividend.csv"

    ex_div_date = dates[ex_div_bar]
    ex_dt = datetime.strptime(ex_div_date, "%Y-%m-%d")
    announce_dt = ex_dt - timedelta(days=3)
    record_dt = ex_dt - timedelta(days=1)

    with open(div_path, 'w', newline='') as f:
        f.write("symbol,announce_date,report_year,ex_dividend_date,record_date,"
                "implement_date,bonus_per_10,transfer_per_10,cash_per_10,"
                "allot_per_10,allot_price,ex_div_price,action_type\n")
        f.write(f"{symbol},{announce_dt.strftime('%Y-%m-%d')},2023,"
                f"{ex_div_date},{record_dt.strftime('%Y-%m-%d')},{ex_div_date},"
                f"0,0,{cash_per_10},0,0,0,0\n")

    print(f"  分红: {symbol} ex_div={ex_div_date} cash_per_10={cash_per_10}")
    return div_path


def _strategy_builder(strategy_id, source="A_hfq"):
    """节点测试策略的公共外壳：名称、描述、回测区间、数据源。"""
    return StrategyBuilder(
        strategy_id,
        f"节点测试_{strategy_id}",
        description="节点单元测试",
        backtest={
            "start": START_DATE.strftime("%Y-%m-%d"),
            "end": (START_DATE + timedelta(days=250)).strftime("%Y-%m-%d"),
        },
        source=source,
    )


def std_strategy(symbol, window, dataset_id):
    return single_node_strategy(symbol, dataset_id, f"std_{window}",
                                "STD", f"{window}d", f"debug_std_{window}")


def ma_strategy(symbol, window, dataset_id):
    return single_node_strategy(symbol, dataset_id, f"ma_{window}",
                                "MA", f"{window}d", f"debug_ma_{window}")


def return_strategy(symbol, n, dataset_id):
    return single_node_strategy(symbol, dataset_id, f"return_{n}",
                                "Return", f"{n}d", f"debug_return_{n}")


def zscore_strategy(symbol, window, dataset_id):
    return single_node_strategy(symbol, dataset_id, f"zscore_{window}",
                                "ZScore", f"{window}d", f"debug_zscore_{window}")


def r2_strategy(symbol, window, dataset_id):
    return single_node_strategy(symbol, dataset_id, f"r2_{window}",
                                "R2", f"{window}d", f"debug_r2_{window}")


def median_strategy(symbol, window, dataset_id):
    """Median 测试策略: Input(close) → FunctionNode(Median) → Debug"""
    return single_node_strategy(symbol, dataset_id, f"median_{window}",
                                "Median", f"{window}d", f"debug_median_{window}")


def cummax_strategy(symbol, window, dataset_id):
    """CumMax 测试策略: Input(close) → FunctionNode(CumMax) → Debug

    与 Median 同构，但测的是单调队列的回退逻辑：窗口滑出后必须忘掉历史最高价。
    """
    return single_node_strategy(symbol, dataset_id, f"cummax_{window}",
                                "CumMax", f"{window}d", f"debug_cummax_{window}")


def vpcorr_strategy(symbol, window, dataset_id):
    """VPCorr 测试策略: Input(close+volume) → FunctionNode(VPCorr) → Debug

    与单输入函数不同，VPCorr 需要同时连接 close 和 volume。
    InputNode 默认输出所有 OHLCV 字段，FunctionNode 按字段名自动分配到槽位。
    """
    import re
    window_num = re.match(r'(\d+)', f"{window}d").group(1)
    node_label = f"VPCorr({window_num})"
    return (_strategy_builder(f"test_{dataset_id}_vpcorr_{window}")
            .input("1", symbol=symbol)
            .function("2", "VPCorr", f"{window}d", label=node_label)
            .debug("3", f"debug_vpcorr_{window}")
            .signal("4", symbol)
            .portfolio("5")
            .execution("6")
            .connect("1", "2", prop="close")    # close → price 槽位
            .connect("1", "2", prop="volume")   # volume → volume 槽位
            .connect("2", "3")
            .connect("1", "4", prop="close")
            .connect("4", "5")
            .connect("5", "6")
            .build())


def formula_add_strategy(symbol, dataset_id):
    """F-1: ma5[t] + std5[t]  基本加法（用 [t] 强制走时间索引路径，避免 Vector<double> 类型问题）"""
    return formula_strategy(symbol, dataset_id, "formula_add", "ma5[t] + std5[t]",
                            [("ma5", "MA", "5d"), ("std5", "STD", "5d")],
                            debug_label="debug_formula_add")


def formula_precedence_strategy(symbol, dataset_id):
    """F-2: ma5[t] * 2 + std5[t]  运算符优先级"""
    return formula_strategy(symbol, dataset_id, "formula_prec", "ma5[t] * 2 + std5[t]",
                            [("ma5", "MA", "5d"), ("std5", "STD", "5d")],
                            debug_label="debug_formula_prec")


def formula_composite_strategy(symbol, dataset_id):
    """F-3: (ma5[t] + ma15[t]) / 2  复合 + 括号"""
    return formula_strategy(symbol, dataset_id, "formula_comp", "(ma5[t] + ma15[t]) / 2",
                            [("ma5", "MA", "5d"), ("ma15", "MA", "15d")],
                            debug_label="debug_formula_comp")


def formula_close_strategy(symbol, dataset_id):
    """F-4: close[t] - ma5[t]  close 引用 + 单变量"""
    return formula_strategy(symbol, dataset_id, "formula_close", "close[t] - ma5[t]",
                            [("ma5", "MA", "5d")],
                            debug_label="debug_formula_close",
                            connect_input=True)


def formula_envelope_strategy(symbol, dataset_id):
    """F-5: ma15 + 2*std15  包络上轨 (= Bollinger Band Upper)"""
    return formula_strategy(symbol, dataset_id, "formula_envelope", "ma15[t] + 2 * std15[t]",
                            [("ma15", "MA", "15d"), ("std15", "STD", "15d")],
                            debug_label="debug_formula_env")


def formula_timeidx_strategy(symbol, dataset_id):
    """F-6: return[t-1]  时间索引 (前一根 bar 的收益率)"""
    return formula_strategy(symbol, dataset_id, "formula_t1", "ret1[t-1]",
                            [("ret1", "Return", "1d")],
                            debug_label="debug_formula_t1")


def formula_compare_strategy(symbol, dataset_id):
    """F-7: ma5[t] > ma15[t]  比较信号 (返回 0/1)"""
    return formula_strategy(symbol, dataset_id, "formula_cmp", "ma5[t] > ma15[t]",
                            [("ma5", "MA", "5d"), ("ma15", "MA", "15d")],
                            debug_label="debug_formula_cmp")


def formula_multinode_strategy(symbol, dataset_id):
    """F-8: ma5[t] * std15[t]  多节点组合（MA 与 STD 相乘）"""
    return formula_strategy(symbol, dataset_id, "formula_multi", "ma5[t] * std15[t]",
                            [("ma5", "MA", "5d"), ("std15", "STD", "15d")],
                            debug_label="debug_formula_multi")


def cusum_returns(prices):
    """按 FunctionNode::Return 的语义计算对数收益率，并剔除不可用值。

    首 bar 无历史价格（C++ 返回 NaN）；0 价格（anomaly 数据集）会产生 ±inf/NaN。
    两者在 CUSUMNode::ProcessSingleAsset 中都被 std::isfinite 过滤，这里同样剔除。
    """
    p = np.asarray(prices, dtype=float)
    with np.errstate(divide="ignore", invalid="ignore"):
        r = np.log(p[1:] / p[:-1])
    return r[np.isfinite(r)]


def cusum_strategy(symbol, n, dataset_id, prices):
    """CUSUM 节点测试: Input → Return(1) → CUSUM → Debug + Signal/Portfolio/Execution

    mu/sigma 由生成数据的实测收益率标定（ddof=0，与 CUSUMDetector::calibrate 一致）
    并显式写入策略 JSON。这样 mu/sigma 成为两侧共享的字面量，节点走
    "不校准 + 纯累积"（calibrate_period=0, threshold_cap=0）路径，可与 Python 参考
    逐 bar 精确比对；校准路径由 test_cusum_calibrate.py 覆盖。

    ⚠️ 此处 mu 取自全序列均值（含未来信息），仅用于把"公式正确性"与"校准正确性"
    解耦，禁止作为生产策略参数。
    """
    r = cusum_returns(prices)
    mu = float(r.mean()) if r.size else 0.0
    sigma = float(r.std()) if r.size else 1.0
    if sigma < 1e-10:
        sigma = 1e-10

    strat_id = f"test_{dataset_id}_cusum_{n}"
    debug_label = f"debug_cusum_{n}"
    return (_strategy_builder(strat_id)
            .input("1", symbol=symbol)
            .function("2", "Return", f"{n}d", label=f"Return({n})")
            .cusum("3", min_obs=10, calibrate_period=0, mu=mu, sigma=sigma,
                   threshold_cap=0.0)
            .debug("4", debug_label)
            .signal("5", symbol)
            .portfolio("6")
            .execution("7")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .connect("3", "4")
            .connect("1", "5", prop="close")
            .connect("5", "6")
            .connect("6", "7")
            .build())


def emd_strategy(symbol, num_imfs, dataset_id):
    """EMD 节点测试: Input → EMD(close) → Debug + Signal/Portfolio/Execution"""
    strat_id = f"test_{dataset_id}_emd_{num_imfs}"
    debug_label = f"debug_emd_{num_imfs}"
    return (_strategy_builder(strat_id)
            .input("1", symbol=symbol)
            .emd("2", num_imfs=num_imfs)
            .debug("3", debug_label)
            .signal("4", symbol)
            .portfolio("5")
            .execution("6")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .connect("1", "4", prop="close")
            .connect("4", "5")
            .connect("5", "6")
            .build())


def formula_strategy(symbol, dataset_id, node_suffix, expression, upstream_specs, debug_label, connect_input=False):
    """Formula 测试策略

    upstream_specs: [(var_name, func_name, range_val), ...]
      - var_name: FormulaNode 中的引用名（同时也是上游节点 label 和 output key）
      - func_name: MA/STD/ZScore
      - range_val: "5d"/"15d"
    connect_input: 是否连接 InputNode → FormulaNode（用于 close/open 等原始行情引用）

    策略图: Input → [Upstream1, Upstream2, ...] → Formula → Debug + Signal → Portfolio → Execution
    """
    builder = _strategy_builder(f"test_{dataset_id}_{node_suffix}")
    builder.input("1", symbol=symbol)

    next_id = 2
    upstream_ids = []
    for var_name, func_name, range_val in upstream_specs:
        uid = str(next_id)
        builder.function(uid, func_name, range_val, label=var_name)
        upstream_ids.append(uid)
        # 每个 upstream 都要接 Input 的 close
        builder.connect("1", uid, prop="close")
        next_id += 1

    # Formula 节点
    formula_id = str(next_id)
    builder.formula(formula_id, expression, label=debug_label.replace("debug_", ""))
    for uid in upstream_ids:
        builder.connect(uid, formula_id)
    # 如果需要连接 InputNode（如 close 引用）
    if connect_input:
        builder.connect("1", formula_id, prop="close")
    next_id += 1

    # Debug 节点
    debug_id = str(next_id)
    builder.debug(debug_id, debug_label)
    builder.connect(formula_id, debug_id)
    next_id += 1

    # Signal/Portfolio/Execution (最小交易链)
    signal_id = str(next_id)
    builder.signal(signal_id, symbol)
    builder.connect("1", signal_id, prop="close")
    next_id += 1

    portfolio_id = str(next_id)
    builder.portfolio(portfolio_id)
    builder.connect(signal_id, portfolio_id)
    next_id += 1

    execution_id = str(next_id)
    builder.execution(execution_id)
    builder.connect(portfolio_id, execution_id)

    return builder.build()


def single_node_strategy(symbol, dataset_id, node_suffix, func_name, range_val, debug_label):
    """通用单节点测试策略: Input → FunctionNode → Debug + Signal → Portfolio → Execution"""
    # label 只用数字窗口（不含 d 后缀），如 "STD(15)" 而非 "STD(15d)"
    import re
    window_num = re.match(r'(\d+)', range_val).group(1)
    node_label = f"{func_name}({window_num})"
    return (_strategy_builder(f"test_{dataset_id}_{node_suffix}")
            .input("1", symbol=symbol)
            .function("2", func_name, range_val, label=node_label)
            .debug("3", debug_label)
            .signal("4", symbol)
            .portfolio("5")
            .execution("6")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .connect("1", "4", prop="close")
            .connect("4", "5")
            .connect("5", "6")
            .build())


def main():
    datasets = {
        "sine": {
            "symbol": "sz.800001", "name": "正弦+噪声",
            "generator": gen_sine, "kwargs": {"n_bars": 200, "amplitude": 10.0, "period": 40, "noise_std": 0.5}
        },
        "step": {
            "symbol": "sz.800002", "name": "阶梯跳变",
            "generator": gen_step, "kwargs": {"n_bars": 200, "jump_at": 100, "jump_size": 10.0}
        },
        "reversal": {
            "symbol": "sz.800003", "name": "趋势+反转",
            "generator": gen_reversal, "kwargs": {"n_bars": 200}
        },
        "deterministic": {
            "symbol": "sz.800004", "name": "确定性序列",
            "generator": gen_deterministic, "kwargs": {"n_bars": 200}
        },
        "anomaly": {
            "symbol": "sz.800005", "name": "含异常值",
            "generator": gen_anomaly, "kwargs": {"n_bars": 200, "anomaly_bar": 100}
        },
    }

    all_strategies = {}
    # 节点类型定义: (策略生成函数, 参数, 节点标签)
    node_configs = [
        (std_strategy, [5], "STD(5)"),
        (std_strategy, [15], "STD(15)"),
        (ma_strategy, [5], "MA(5)"),
        (ma_strategy, [15], "MA(15)"),
        (return_strategy, [1], "Return(1)"),
        (return_strategy, [3], "Return(3)"),
        (zscore_strategy, [5], "ZScore(5)"),
        (zscore_strategy, [15], "ZScore(15)"),
        (r2_strategy, [5], "R2(5)"),
        (r2_strategy, [15], "R2(15)"),
        # Median 节点 (滚动中位数) — 2026-08-20 新增
        (median_strategy, [5], "Median(5)"),
        (median_strategy, [15], "Median(15)"),
        # CumMax 节点 (区间最高价) — 配合 drawdown 使用
        (cummax_strategy, [5], "CumMax(5)"),
        (cummax_strategy, [15], "CumMax(15)"),
        (cummax_strategy, [60], "CumMax(60)"),
        # VPCorr 节点 (量价相关性)
        (vpcorr_strategy, [5], "VPCorr(5)"),
        (vpcorr_strategy, [15], "VPCorr(15)"),
        # Formula 测试 (不需要参数)
        (formula_add_strategy, [], "Formula: MA(5)+STD(5)"),
        (formula_precedence_strategy, [], "Formula: MA(5)*2+STD(5)"),
        (formula_composite_strategy, [], "Formula: (MA(5)+MA(15))/2"),
        (formula_close_strategy, [], "Formula: close-MA(5)"),
        (formula_envelope_strategy, [], "Formula: MA(15)+2*STD(15)"),
        (formula_timeidx_strategy, [], "Formula: ret1[t-1]"),
        (formula_compare_strategy, [], "Formula: ma5[t]>ma15[t]"),
        (formula_multinode_strategy, [], "Formula: ma5[t]*std15[t]"),
        # CUSUM 节点
        (cusum_strategy, [1], "CUSUM(Return(1))"),
        # EMD 节点
        (emd_strategy, [5], "EMD(numIMFs=5)"),
    ]

    for ds_id, ds in datasets.items():
        symbol = ds["symbol"]
        print(f"\n[{ds_id}] {ds['name']} @ {symbol}")
        prices = ds["generator"](**ds["kwargs"])
        anomaly_bar = ds["kwargs"].get("anomaly_bar")
        dates = write_csv(symbol, prices, START_DATE,
                          HFQ_DIR / f"{symbol}.csv", ORG_DIR / f"{symbol}.csv", anomaly_bar)
        print(f"  CSV: {len(prices)} bars")

        # sz.800001 生成分红测试数据 (ex_div at bar 100, 每10股派5元)
        if symbol == "sz.800001":
            generate_dividend_data(
                symbol, dates,
                HFQ_DIR / f"{symbol}.csv", ORG_DIR / f"{symbol}.csv",
                ex_div_bar=100, cash_per_10=5.0)

        for gen_func, args, node_label in node_configs:
            # cusum_strategy 需要实测收益率来标定 mu/sigma（见其 docstring）
            if gen_func is cusum_strategy:
                strat = gen_func(symbol, *args, ds_id, prices)
            else:
                strat = gen_func(symbol, *args, ds_id)
            # 从函数名和参数推断文件名
            func_name = gen_func.__name__.replace("_strategy", "")
            if args:
                arg_str = "_".join(str(a) for a in args)
                fname = f"{ds_id}_{func_name}_{arg_str}.json"
                strat_id = f"{ds_id}_{func_name}_{arg_str}"
            else:
                fname = f"{ds_id}_{func_name}.json"
                strat_id = f"{ds_id}_{func_name}"
            with open(TEST_DATA_DIR / fname, 'w') as f:
                json.dump(strat, f, indent=2, ensure_ascii=False)
            all_strategies[strat_id] = {"dataset": ds_id, "symbol": symbol, "node": node_label}
            print(f"  策略: {fname}")

    # ── 配对交易数据集（两条腿 + 价差/配对策略）──
    print("\n配对数据集:")
    pair_summary = generate_pair_dataset()
    all_strategies["pair_spread"] = {"dataset": "pair", "symbol": PAIR_SYMBOL_A,
                                     "node": "pair_spread"}
    all_strategies["pair_trading"] = {"dataset": "pair", "symbol": PAIR_SYMBOL_A,
                                      "node": "pair"}

    summary = {"datasets": {k: {"symbol": v["symbol"], "name": v["name"]} for k, v in datasets.items()},
               "strategies": all_strategies,
               "pair": pair_summary}
    with open(TEST_DATA_DIR / "test_data_summary.json", 'w') as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    print(f"\n汇总: test_data_summary.json")
    print(f"共 {len(datasets)} 数据集, {len(all_strategies)} 策略")


if __name__ == "__main__":
    main()
