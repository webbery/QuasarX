#!/usr/bin/env python3
"""生成 PortfolioNode 各仓位算法（sizing_method）测试用的合成行情。

背景
----
PortfolioNode 支持 5 种仓位算法：equal / kelly / volatility_target /
strength / mvo（doc/mvo.md）。它们都没有既有测试覆盖，而每种算法要观测的
东西不一样：

=============  ==========================================================
算法           需要什么样的数据 / 什么上下文键
=============  ==========================================================
equal          任意行情（等权是基线，回归对照）
kelly         需要 ``{symbol}.win_rate`` —— 目前**没有任何节点写它**，
               所以实际恒等于等权（doc/mvo.md §7.3）
volatility_target  需要 ``{symbol}.volatility`` —— 同样没人写，恒等于等权
strength       需要 ``{symbol}.strength`` —— 只有 PairNode 会写
mvo           ① 漂移排序事先可知 ② 买卖信号严格交替 ③ 权重要真的动
=============  ==========================================================

数据设计
--------
6 个标的，240 根日线。**进入 bar t 的对数收益**::

    r_i(t) = drift_i(t) + zig(t) + noise_i(t)
    p_i(0) = 5.00
    p_i(t) = p_i(t-1) * exp(r_i(t))

（服务端 MVO 用的面板行 s = log(close[s+1]/close[s])，即面板第 s 行对应
"进入 bar s+1 的收益"。）

公共成分
--------
* ``zig(t) = +0.01`` (t 偶) / ``−0.01`` (t 奇) —— **公共锯齿**。
  幅度（1%）远大于漂移+噪声（≤0.72%），所以 ``close[t] > close[t−1]`` 的
  符号完全由锯齿决定 → **买卖信号严格交替**，且跨标的同相（同一根 bar 上
  4 个标的要么一起买、要么一起卖）。
  公共锯齿同时充当"市场因子"：因子代理 = 全池等权收益 ≈ 锯齿，滚动 OLS 的
  β ≈ 1，MVO 的因子剥离路径被真实走到（而不是退化成 β=1 的平凡情形）。
* ``noise_i(t) ~ N(0, 0.002)`` —— 标的特有噪声。

标的画像
--------
=====  ==========  ==========  ==========  ==========  ====================
编号   漂移段1     漂移段2     段1 年化    段2 年化    主要服务
=====  ==========  ==========  ==========  ==========  ====================
.920001  +0.0012    −0.0008    +30.2%     −20.2%     MVO 排序 / 权重漂移
.920002  +0.0004    +0.0002    +10.1%      +5.0%     MVO 排序
.920003   0.0000    −0.0004      0.0%     −10.1%     kelly / mvo 中性档
.920004  −0.0008    +0.0012    −20.2%     +30.2%     MVO 排序翻转
.920005  +0.0008    +0.0008    +20.2%     +20.2%     高波动档
.920006  +0.0008    +0.0008    +20.2%     +20.2%     低波动档
=====  ==========  ==========  ==========  ==========  ====================

* .920001 / .920004 在 t=120 处换挡 → 排序翻转 → **MVO 权重大幅且确定地
  漂移**，换手惩罚才有东西可压（首根 MVO bar 的 w_prev 恒为 0，而
  ‖w − 0‖₁ = Σw = e 是常数，惩罚项对解完全无影响 —— 必须有多根 bar）。
* .920005 / .920006 漂移相同、噪声差 4 倍 → 波动率差异明显（波动率目标
  类算法能看出差别；等权时看不出）。

为什么是这些数
--------------
用 numpy 复刻 C++ 的 MvoEstimator 逐 bar 递推验证过（见 test 文件头
"数值口径" 一节）：这套参数下
  * 涨跌严格交替成立（锯齿 1% > 漂移 0.12% + 3σ 0.6%）；
  * bar 100（第一根 MVO bar）处 μ 的年化离散度 ≈ 0.33 → 权重明显偏离等权；
  * bar 100 → bar 220 的 μ 最大摆幅 ≈ 1.6 → 权重轨迹长度足够大，
    换手惩罚的开/关能拉开数量级的差距（路径长 59 → 1.2 → 0）。
* **价格基数 5.00**：回测资金 500,000（BACKTEST_INITIAL_CAPITAL），
  4 标的均分时每标的 125,000 → 25,000 股。整手取整（100 股）的相对误差
  ≈ 0.4%，测试容差按此设定。

输出
----
* ``{db_path}/A_hfq/<symbol>.csv`` 与 ``{db_path}/AStock/<symbol>.csv``
  —— 后复权与原始价写同一份（不造除权，测试不需要）。
* ``node_test_data/portfolio_strategy.json`` —— 参考策略图。**必须存在**：
  conftest 的 ``upload_test_data`` fixture 靠扫描本目录下的 JSON 提取标的
  符号，再把对应 CSV 灌进 DuckDB；而服务端 ``LoadHistoryDataWithFreq``
  里读 DuckDB 失败后的 CSV 回退路径是**注释掉的**（``// goto fallback_csv``），
  没有这份 JSON 就没有行情，回测会静默地"零成交"。
* ``node_test_data/portfolio_expected.json`` —— 各算法测试用的**基准事实**：
  逐标的画像、排序、EWMA 协方差（numpy 口径）、ρ̄ 等。

用法::

    cd service/test/testcases/node_test_data
    python generate_portfolio_data.py
"""

import csv
import json
import sys
from datetime import datetime, timedelta
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent.parent))
from tool import _DATA_DIR as SERVICE_DATA_DIR  # noqa: E402

HFQ_DIR = SERVICE_DATA_DIR / "A_hfq"
ORG_DIR = SERVICE_DATA_DIR / "AStock"
TEST_DATA_DIR = Path(__file__).parent
START_DATE = datetime(2024, 1, 1)

# ── 数据参数 ────────────────────────────────────────────────────────────
N_BARS = 240
REGIME_SWITCH = 120          # 段1 / 段2 的分界 bar（按 bar 序号）
P0 = 5.0                     # 初始价格
ZIGZAG = 0.01                # 公共锯齿幅度（±1%）
NOISE_STD = 0.002            # 标的特有噪声 σ
HIGH_VOL_STD = 0.008         # .920005 的噪声 σ（低波动组的 4 倍）
SEED = 20240501

# 逐标的画像：symbol → (段1 漂移, 段2 漂移, 噪声 σ)
PROFILES = [
    # symbol        seg1      seg2      noise σ
    ("sz.920001",  0.0012,  -0.0008,  NOISE_STD),
    ("sz.920002",  0.0004,   0.0002,  NOISE_STD),
    ("sz.920003",  0.0000,  -0.0004,  NOISE_STD),
    ("sz.920004", -0.0008,   0.0012,  NOISE_STD),
    ("sz.920005",  0.0008,   0.0008,  HIGH_VOL_STD),
    ("sz.920006",  0.0008,   0.0008,  NOISE_STD),
]

# 便于按语义取子集
MVO_SYMBOLS = [p[0] for p in PROFILES[:4]]       # 前 4 个：排序 + 换挡
VOL_SYMBOLS = ["sz.920005", "sz.920006"]        # 同漂移、不同波动率


def build_returns():
    """构造 (标的, bar) 的对数收益矩阵 —— 元素是"进入该 bar"的收益。

    zig 的相位刻意与 bar 序号对齐：偶数 bar 的收益为正 → ``close[t] >
    close[t-1]`` 在偶数 bar 成立 → 买卖信号严格交替。
    """
    rng = np.random.default_rng(SEED)
    t = np.arange(N_BARS)
    zig = np.where(t % 2 == 0, ZIGZAG, -ZIGZAG)

    n = len(PROFILES)
    returns = np.empty((n, N_BARS))
    for i, (_sym, d1, d2, noise_std) in enumerate(PROFILES):
        drift = np.where(t < REGIME_SWITCH, d1, d2)
        noise = rng.normal(0.0, noise_std, size=N_BARS)
        returns[i] = drift + zig + noise
    return returns


def build_prices(returns):
    """由对数收益构造价格路径（收益是"进入 bar t"的）。"""
    prices = np.empty_like(returns)
    prices[:, 0] = P0
    for t in range(1, returns.shape[1]):
        prices[:, t] = prices[:, t - 1] * np.exp(returns[:, t])
    return prices


def write_csv(symbol, prices, hfq_path, org_path):
    """写行情 CSV（格式与 generate_test_data.py 一致）。

    后复权目录与原始价目录写同一份价格：本测试不造除权事件。
    """
    rng = np.random.default_rng(SEED)
    for path in (hfq_path, org_path):
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", newline="") as f:
            writer = csv.writer(f)
            writer.writerow(["datetime", "open", "close", "high", "low",
                             "volume", "turnover"])
            current = START_DATE
            for close in prices:
                while current.weekday() >= 5:
                    current += timedelta(days=1)
                date_str = current.strftime("%Y-%m-%d")

                open_price = close * (1 + rng.normal(0, 0.001))
                high = max(open_price, close) * (1 + abs(rng.normal(0, 0.002)))
                low = min(open_price, close) * (1 - abs(rng.normal(0, 0.002)))
                volume = int(rng.uniform(1_000_000, 5_000_000))
                turnover = round(volume * close, 2)

                writer.writerow([date_str, round(open_price, 2), round(close, 2),
                                 round(high, 2), round(low, 2), volume, turnover])
                current += timedelta(days=1)


def ewma_covariance(returns, halflife=60.0):
    """numpy 口径的 EWMA 协方差（年化 ×252）。

    口径对齐 MvoRisk::update 的 EWMA 路径（不含常数相关收缩）：
    ``Σ_t = (1−λ)Σ_{t−1} + λ(x_t − μ̄_{t−1})(x_t − μ̄_{t−1})'``，
    λ = 1 − 0.5^(1/halflife)。MvoRisk 把非有限值当 0（"当日无变动"），
    这里同样处理。

    ``returns`` 是 (bar, 标的) 形状。
    """
    x = np.nan_to_num(returns, nan=0.0)
    lam = 1.0 - 0.5 ** (1.0 / max(halflife, 1.0))
    mean = np.zeros(x.shape[1])
    cov = np.zeros((x.shape[1], x.shape[1]))
    for t in range(x.shape[0]):
        dev = x[t] - mean
        outer = np.outer(dev, dev)
        if t == 0:
            mean = x[t]
            cov = outer
        else:
            mean = (1 - lam) * mean + lam * x[t]
            cov = (1 - lam) * cov + lam * outer
    return cov * 252.0


def average_pairwise_correlation(returns):
    """常数相关收缩目标里的 ρ̄ —— 只取 i<j 的上三角。

    这是 doc/mvo.md §5.1 记录的实验台 bug 的正确口径：
    ``lab/pf/risk.py`` 对全部非对角元素求和却除以"对数" n(n−1)/2，
    把 ρ̄ 放大了 2 倍。C++ 实现与本文件都按 i<j 取平均。
    """
    corr = np.corrcoef(np.nan_to_num(returns, nan=0.0), rowvar=False)
    n = corr.shape[0]
    iu = np.triu_indices(n, k=1)
    return float(corr[iu].mean())


def annualized_vol(returns):
    """年化波动率（对数收益 × √252）。"""
    return float(np.std(returns, ddof=1) * np.sqrt(252))


def write_reference_strategy(path):
    """写参考策略图 JSON。

    这份 JSON 的作用不是"跑一次策略"，而是**让 conftest 找到这些标的**：
    ``upload_test_data`` fixture 扫描 testcases/node_test_data/ 下的 JSON，
    抽出 ``sz.920001`` 这类符号，把 build/data 下的 CSV 灌进 DuckDB 的
    ``stock_1d`` 表。服务端读行情时 CSV 回退路径被注释掉了，所以没有这一步
    就没有行情、回测会"零成交"。

    策略本身与 test_portfolio.py 的构图一致（行情 → 信号 → 组合 → 执行）。
    """
    sys.path.insert(0, str(Path(__file__).parent.parent))
    from strategy_builder import StrategyBuilder

    builder = (StrategyBuilder(
                   "portfolio_mvo",
                   "组合仓位参考策略",
                   description="PortfolioNode sizing_method 端到端测试用的"
                               "参考策略图（同时充当 conftest 的标的索引）",
                   backtest={"start": "2024-01-01", "end": "2024-12-31"},
                   source="A_hfq")
               .portfolio("9", position_ratio=1.0, sizing_method="mvo"))
    for i, (symbol, _d1, _d2, _noise) in enumerate(PROFILES, start=1):
        nid = str(i)
        builder.input(nid, symbol=symbol)
        builder.connect(nid, "8", prop="close")
    builder.signal("8", PROFILES[0][0],
                   buy="close[t-100] > 0 and close[t] > close[t-1]",
                   sell="close[t-100] > 0 and close[t] < close[t-1]")
    builder.connect("8", "9")
    builder.execution("10")
    builder.connect("9", "10")
    builder.dump(path)


def main():
    returns = build_returns()
    prices = build_prices(returns)

    HFQ_DIR.mkdir(parents=True, exist_ok=True)
    ORG_DIR.mkdir(parents=True, exist_ok=True)

    for i, (symbol, _d1, _d2, _noise) in enumerate(PROFILES):
        write_csv(symbol, prices[i], HFQ_DIR / f"{symbol}.csv",
                  ORG_DIR / f"{symbol}.csv")
        print(f"  {symbol}: {N_BARS} bars, close {prices[i][0]:.2f} → "
              f"{prices[i][-1]:.2f}, 年化波动 {annualized_vol(returns[i]):.1%}")

    # ── 基准事实 ──
    # 面板行 s = 进入 bar s+1 的收益
    panel = np.log(prices[:, 1:] / prices[:, :-1]).T              # (T-1, n)
    seg1 = panel[:REGIME_SWITCH - 1]
    seg2 = panel[REGIME_SWITCH - 1:]

    mvo_idx = [i for i, p in enumerate(PROFILES) if p[0] in MVO_SYMBOLS]

    expected = {
        "_comment": "PortfolioNode 各仓位算法的基准事实，"
                    "由 generate_portfolio_data.py 生成",
        "symbols": [p[0] for p in PROFILES],
        "n_bars": N_BARS,
        "regime_switch": REGIME_SWITCH,
        "p0": P0,
        "zigzag": ZIGZAG,
        "noise_std": NOISE_STD,
        "seed": SEED,
        "profiles": {
            p[0]: {
                "drift_seg1": p[1],
                "drift_seg2": p[2],
                "noise_std": p[3],
                "annual_drift_seg1": round(p[1] * 252, 6),
                "annual_drift_seg2": round(p[2] * 252, 6),
                "annual_vol": round(annualized_vol(returns[i]), 6),
            }
            for i, p in enumerate(PROFILES)
        },
        # ── MVO 专用 ──
        "mvo_symbols": MVO_SYMBOLS,
        "vol_symbols": VOL_SYMBOLS,
        # 段1 / 段2 的排序（按漂移从高到低，下标是 MVO_SYMBOLS 内的位置）
        "mvo_ranking_seg1": [int(i) for i in
                             np.argsort([PROFILES[i][1] for i in mvo_idx])[::-1]],
        "mvo_ranking_seg2": [int(i) for i in
                             np.argsort([PROFILES[i][2] for i in mvo_idx])[::-1]],
        # 常数相关收缩的 ρ̄（i<j 上三角均值）
        "rho_bar_full_sample": average_pairwise_correlation(panel),
        "ewma_cov_full": ewma_covariance(panel).tolist(),
        "ewma_cov_seg1": ewma_covariance(seg1).tolist(),
        "ewma_cov_seg2": ewma_covariance(seg2).tolist(),
    }

    out_path = TEST_DATA_DIR / "portfolio_expected.json"
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(expected, f, ensure_ascii=False, indent=1)

    # 参考策略图（conftest 靠它发现标的并灌 DuckDB）
    strategy_path = TEST_DATA_DIR / "portfolio_strategy.json"
    write_reference_strategy(strategy_path)

    print(f"\n  ρ̄ (i<j 上三角) = {expected['rho_bar_full_sample']:.6f}")
    print(f"  MVO 段1 排序(下标): {expected['mvo_ranking_seg1']}")
    print(f"  MVO 段2 排序(下标): {expected['mvo_ranking_seg2']}")
    print(f"  基准事实 → {out_path}")
    print(f"  参考策略 → {strategy_path}")


if __name__ == "__main__":
    main()
