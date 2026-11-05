#!/usr/bin/env python3
"""PortfolioNode 仓位算法（sizing_method）端到端测试。

覆盖对象
--------
PortfolioNode 的仓位分配分支：equal / kelly / volatility_target / strength /
mvo。mvo 是 doc/mvo.md 描述的新实现（卡尔曼均值 + EWMA 协方差 + OSQP），
本文件是它的第一组回归测试。

数据
----
合成行情由 ``testcases/node_test_data/generate_portfolio_data.py`` 生成：
6 个标的 × 240 根日线，价格路径里嵌了一个**公共锯齿**（±1%，周期 2 根 bar）。
三个后果都是刻意设计的：

1. **买卖信号严格交替** —— 锯齿幅度远大于漂移+噪声，所以
   ``close[t] > close[t-1]`` 的符号完全由锯齿决定 → 偶数 bar 全部买入、
   奇数 bar 全部卖出。这条是必需的：MVO 的候选集是「有 BUY 信号**且当前
   无持仓**」的标的，一旦第一根 bar 就建仓，之后所有 bar 的候选集都是空，
   MVO 再也跑不起来（doc/mvo.md §3.2 的阶段一行为）。
2. **前 100 根 bar 不给信号** —— 信号表达式里的 ``close[t-100] > 0`` 在
   t<100 时越界取到 NaN，比较恒为 false（FormulaParser 的既定语义）。
   于是第一根 MVO bar 落在 t=100，此时收益面板有 100 行 ≥ min_samples，
   而所有标的仍然空仓 → 4 个候选全部可用。
3. **权重真的会动** —— .920001/.920004 在 t=120 处漂移换挡，排序翻转，
   μ 随之大幅摆动 → 换手惩罚才有东西可压。

观测通道
--------
PortfolioNode 把执行计划摘要写进 context（``{symbol}.plan_action`` /
``.plan_qty`` / ``.plan_price``），DebugNode 把它导出成 CSV。于是：

    资金_i = plan_qty_i × plan_price_i
    权重_i = 资金_i / Σ_j 资金_j        （只在买入 bar 上算）

权重用**归一化比值**而不是绝对金额，有两个原因：

* 目标资金 ``targetCapital = 可用现金 × positionRatio`` 随回测推进会缩水
  （买入花掉现金，且计划价用上一根 bar 的收盘价、成交用本根 bar 的收盘价，
  每次买卖循环大约损耗 1%），绝对金额不可比；
* MVO 的 QP 约束是 ``Σw = e = 1``（e = 目标敞口，默认 1.0），所以候选集
  内部的资金占比就是权重，与 e 无关。

已知精度限制
------------
下单数量向下取整到 100 股整数倍（``PortfolioNode.cpp`` 的 BUY 分支），
单只标的的资金相对误差 ≈ 100×价格/资金 ≈ 0.4%（回测资金 50 万、4 标的
均分、价格约 5 元）。所有权重断言的容差按此设定（≥1%）。

另外，**权重为 0 的标的在计划里表现为"没有这一项"**（资金 0 → 数量 0 →
"Quantity 0 too small" → 该项被丢弃 → plan_action=0、plan_qty=0）。所以
买入 bar 上 plan_action=0 既可能是"没给信号"，也可能是"MVO 给了 0 权重"——
在 MVO 用例里买入 bar 上 4 个标的全都有 BUY 信号，因此可以安全地把
plan_action=0 解读为"权重 0"。

不能从端到端观测到的东西
------------------------
MVO 的 μ、Σ、β 等内部量只存在于 PortfolioNode 内部，端到端只能看到最终
权重。所以本文件断言的是**结构性性质**（约束、边界、回退、参数是否真的
进入优化器），而不是 μ/Σ 的逐值精度 —— 那是 doc/mvo.md §5 的实验室对拍
（Python 参考实现 vs C++）覆盖的层次。

用法::

    pytest test_portfolio.py -v
"""

import numpy as np
import pytest

from strategy_builder import StrategyBuilder
from tool import read_debug_csv, run_backtest_graph

# ── 测试标的（generate_portfolio_data.py 生成）──
MVO_SYMBOLS = ["sz.920001", "sz.920002", "sz.920003", "sz.920004"]

# ── 信号表达式 ──
# close[t-100] > 0：暖机门控（t<100 时越界 → NaN → 比较为 false）
# close[t] > close[t-1]：锯齿决定的涨跌 → 偶数 bar 买、奇数 bar 卖
BUY_EXPR = "close[t-100] > 0 and close[t] > close[t-1]"
SELL_EXPR = "close[t-100] > 0 and close[t] < close[t-1]"

BACKTEST_RANGE = {"start": "2024-01-01", "end": "2024-12-31"}
DEBUG_LABEL = "plan"

# ── 容差 ──
# 单只标的的权重：整手取整导致 ≈0.4% 的相对误差，留足余量
WEIGHT_TOL = 0.02
# 权重之和：4 只标的的取整误差叠加
SUM_TOL = 0.03
# 权重是否"明显非零"（区分 0 权重与有权重）
NONZERO_EPS = 0.05


# ============================================================
# 策略构造
# ============================================================

def _build_strategy(strategy_id, sizing_method="mvo", mvo=None,
                    symbols=None, position_ratio=1.0,
                    max_single_pct=None, max_total_pct=None):
    """构造 行情 → 信号 → 组合 → 执行(+调试) 的策略图。

    节点编号：1..N 行情，8 信号，9 组合，10 执行，11 调试。
    """
    symbols = symbols or MVO_SYMBOLS
    builder = (StrategyBuilder(
                   strategy_id,
                   f"仓位算法测试_{strategy_id}",
                   description="PortfolioNode sizing_method 端到端测试",
                   backtest=BACKTEST_RANGE,
                   source="A_hfq")
               .portfolio("9", position_ratio=position_ratio,
                          sizing_method=sizing_method, mvo=mvo,
                          max_single_pct=max_single_pct,
                          max_total_pct=max_total_pct))
    for i, sym in enumerate(symbols, start=1):
        nid = str(i)
        builder.input(nid, symbol=sym)
        builder.connect(nid, "8", prop="close")
    builder.signal("8", symbols[0], buy=BUY_EXPR, sell=SELL_EXPR)
    builder.connect("8", "9")
    builder.execution("10")
    builder.connect("9", "10")
    builder.debug("11", DEBUG_LABEL)
    builder.connect("9", "11")
    return builder.build()


# 回测结果缓存：同一个配置只跑一次回测，多个用例共享
_RUN_CACHE = {}


def _run(headers, key, **kwargs):
    """跑一次回测并返回 (qty, price, action) 三元组（numpy 数组）。

    key 用于缓存与策略 id（调试 CSV 落在 data/debug/<strategy_id>/）。
    """
    if key in _RUN_CACHE:
        return _RUN_CACHE[key]

    strategy_id = f"pf_sizing_{key}"
    strategy = _build_strategy(strategy_id, **kwargs)
    run_backtest_graph(strategy, headers)

    df = read_debug_csv(strategy_id, DEBUG_LABEL)
    symbols = kwargs.get("symbols") or MVO_SYMBOLS
    qty = np.column_stack([df[f"{s}.plan_qty"].to_numpy(dtype=float)
                            for s in symbols])
    price = np.column_stack([df[f"{s}.plan_price"].to_numpy(dtype=float)
                              for s in symbols])
    action = np.column_stack([df[f"{s}.plan_action"].to_numpy(dtype=float)
                               for s in symbols])
    result = (qty, price, action)
    _RUN_CACHE[key] = result
    return result


def _buy_bars(action):
    """有买入动作的 bar 索引（其余 bar 是卖出/持有，资金口径不可比）。"""
    return [i for i in range(action.shape[0]) if (action[i] == 1).any()]


def _weights(qty, price, action):
    """逐个买入 bar 的权重矩阵（行=bar，列=标的）。

    行内归一化：权重_i = 资金_i / Σ资金。MVO 的约束是 Σw = e（默认 1），
    所以候选集内部的资金占比就是权重。
    """
    capital = qty * price
    out = []
    for i in _buy_bars(action):
        row = capital[i]
        total = row.sum()
        if total <= 0:
            continue
        out.append(row / total)
    return np.array(out) if out else np.zeros((0, capital.shape[1]))


def _path_length(weights):
    """权重轨迹长度：相邻买入 bar 之间 L1 距离之和。

    换手惩罚的作用对象就是"权重的变化速度"，所以这条路径长度就是被压制的量。
    """
    if len(weights) < 2:
        return 0.0
    return float(np.abs(np.diff(weights, axis=0)).sum())


def _assert_uniform(w, expect, note):
    """断言每个买入 bar 上的权重都等于 expect（等权基线 / 回退目标）。"""
    assert len(w) > 10, f"{note}: 买入 bar 太少（{len(w)}），测试数据或信号有问题"
    for i, row in enumerate(w):
        assert np.allclose(row, expect, atol=WEIGHT_TOL), \
            f"{note}: 第 {i} 个买入 bar 的权重应为 {expect}，实际 {np.round(row, 4)}"


# ============================================================
# Fixtures
# ============================================================

@pytest.fixture
def plan_equal(headers):
    """等权基线。"""
    return _run(headers, "equal", sizing_method="equal")


@pytest.fixture
def plan_mvo_default(headers):
    """MVO 默认参数。"""
    return _run(headers, "mvo_default", sizing_method="mvo")


# ============================================================
# 等权基线
# ============================================================

class TestEqualBaseline:
    """等权是所有仓位算法的对照组（也是多数分支的回退目标）。"""

    def test_equal_weight_is_uniform(self, plan_equal):
        _assert_uniform(_weights(*plan_equal), 0.25, "等权")

    @pytest.mark.parametrize("method", ["kelly", "volatility_target", "strength"])
    def test_dead_key_models_fall_back_to_equal(self, headers, method):
        """kelly / volatility_target / strength 当前恒等于等权。

        原因（doc/mvo.md §7.3）：这三个算法分别要读 ``{symbol}.win_rate`` /
        ``{symbol}.volatility`` / ``{symbol}.strength``，而全项目**没有任何
        节点写这三个 context key**（只有 PairNode 会写 .strength），所以模型
        永远拿不到输入、永远回退等权。

        ⚠️ 这条断言锁的是**当前实现状态**，不是"期望行为"。等哪天这三个 key
        被真正写进 context，必须同步改这条用例 —— 否则它会挡住修复。
        """
        qty, price, action = _run(headers, f"{method}_fallback",
                                   sizing_method=method)
        _assert_uniform(_weights(qty, price, action), 0.25,
                        f"{method}（当前应恒等于等权）")


# ============================================================
# MVO 回退路径
# ============================================================

class TestMvoFallback:
    """MVO 拿不到有效输入时退回等权（并伴随告警）。"""

    def test_min_names_fallback(self, headers):
        """候选数不足 min_names → 回退等权。"""
        qty, price, action = _run(
            headers, "mvo_min_names", sizing_method="mvo",
            mvo={"mvo_min_names": 5})
        _assert_uniform(_weights(qty, price, action), 0.25,
                        "候选数 < min_names 应当回退等权")

    def test_min_samples_fallback(self, headers):
        """收益样本不足 min_samples → 回退等权。

        默认 min_samples=60，而第一根 MVO bar（t=100）已经有 100 行面板，
        所以这里把门槛抬到 1000 来触发回退。
        """
        qty, price, action = _run(
            headers, "mvo_min_samples", sizing_method="mvo",
            mvo={"mvo_min_samples": 1000})
        _assert_uniform(_weights(qty, price, action), 0.25,
                        "样本数 < min_samples 应当回退等权")

    def test_infeasible_box_fallback(self, headers):
        """QP 不可行 → 回退等权。

        QP 的约束是 Σw = e = 1 且 0 ≤ w ≤ w_max。4 个标的、w_max=0.2 时
        Σw 的上限只有 0.8 < 1 → 可行域为空 → OSQP 报不可行 → MvoModel
        告警并返回空 map → PortfolioNode 回落到等权。
        """
        qty, price, action = _run(
            headers, "mvo_infeasible", sizing_method="mvo",
            mvo={"mvo_max_weight": 0.2})
        _assert_uniform(_weights(qty, price, action), 0.25,
                        "QP 不可行时应当回退等权")

    def test_unknown_method_falls_back_to_equal(self, headers):
        """未知 sizing_method → 工厂返回等权模型。"""
        qty, price, action = _run(
            headers, "unknown_method", sizing_method="no_such_method")
        _assert_uniform(_weights(qty, price, action), 0.25,
                        "未知 sizing_method 应当回退等权")


# ============================================================
# MVO 权重结构
# ============================================================

class TestMvoWeights:
    """权重必须满足 QP 的结构性约束：0 ≤ w ≤ w_max、Σw = 1。"""

    def test_weights_are_not_equal_weight(self, plan_mvo_default):
        """MVO 确实在重新分配资金（否则下面的约束断言都是空跑）。"""
        w = _weights(*plan_mvo_default)
        assert len(w) > 10
        spread = w.max(axis=1) - w.min(axis=1)
        assert spread.max() > 0.3, \
            f"MVO 的权重与等权几乎一样（最大离散度 {spread.max():.3f}）"

    def test_weights_sum_to_one(self, plan_mvo_default):
        """Σw = e = 1：MVO 不走 applyCaps，预算不会被二次缩放。"""
        w = _weights(*plan_mvo_default)
        sums = w.sum(axis=1)
        assert np.allclose(sums, 1.0, atol=SUM_TOL), \
            f"权重和偏离 1：{sums[:5]}（min={sums.min():.4f}, max={sums.max():.4f}）"

    def test_weight_cap_respected(self, headers):
        """单标的上限 mvo_max_weight 生效。"""
        cap = 0.35
        qty, price, action = _run(headers, "mvo_cap", sizing_method="mvo",
                                  mvo={"mvo_max_weight": cap})
        w = _weights(qty, price, action)
        assert len(w) > 10
        assert w.max() <= cap + WEIGHT_TOL, \
            f"权重 {w.max():.4f} 超过单标的上限 {cap}"

    def test_argmax_structure(self, headers):
        """γ=0、κ=0 时的解析解：资金全压在 μ 最高的两只上。

        γ=0 时目标函数退化成 max μᵀw，约束只有 Σw = 1 与 0 ≤ w ≤ w_max，
        于是解是"贪心装箱"：先给 μ 最大的标的 w_max，剩下的给 μ 第二大的，
        其余为 0。w_max=0.6 时必然是 (0.6, 0.4, 0, 0) 这一形态 —— 与 μ 的
        具体数值无关，所以断言的是**结构**而不是"哪只标的"。
        """
        qty, price, action = _run(
            headers, "mvo_argmax", sizing_method="mvo",
            mvo={"mvo_risk_aversion": 0.0, "mvo_turnover_penalty": 0.0,
                 "mvo_cost_rate": 0.0})
        w = _weights(qty, price, action)
        assert len(w) > 10
        for i, row in enumerate(w):
            nonzero = row[row > NONZERO_EPS]
            assert len(nonzero) == 2, \
                f"第 {i} 个买入 bar 应恰好两只标的拿钱，实际 {np.round(row, 3)}"
            assert abs(nonzero.max() - 0.6) <= WEIGHT_TOL, \
                f"第 {i} 个买入 bar 的最大权重应为 0.6，实际 {nonzero.max():.4f}"
            assert abs(nonzero.min() - 0.4) <= WEIGHT_TOL, \
                f"第 {i} 个买入 bar 的次大权重应为 0.4，实际 {nonzero.min():.4f}"

    def test_mvo_ignores_max_single_pct(self, headers):
        """MVO 不走 max_single_pct 截断（doc/mvo.md §4）。

        QP 自带 0 ≤ w ≤ w_max 与 Σw = e，再叠一层比例缩放会破坏等式约束
        （预算分配会少一截）。所以同样的 max_single_pct=0.1：
          等权 → 每只 10%、总仓位 40%
          MVO  → 仍然 Σw = 1（w_max 的 QP 约束照常生效）
        """
        qty, price, action = _run(headers, "mvo_vs_cap", sizing_method="mvo",
                                  max_single_pct=0.1)
        w = _weights(qty, price, action)
        assert len(w) > 10
        sums = w.sum(axis=1)
        assert np.allclose(sums, 1.0, atol=SUM_TOL), \
            f"MVO 的权重和应保持 1（不受 max_single_pct 影响），实际 {sums[:5]}"


# ============================================================
# MVO 换手惩罚
# ============================================================

class TestMvoTurnover:
    """换手惩罚（κ·c·‖w − w_prev‖₁）必须真的压得住权重漂移。

    目标函数::

        max μᵀw − (γ/2)wᵀΣw − κ·c·‖w − w_prev‖₁

    其中 c 会被 PortfolioNode 折算成 ``c × 252 / H``（H = 预期持有天数）：
    成本是一次性的、目标函数是年化的，不折算会被稀释 1~2 个数量级、惩罚项
    完全不起作用（doc/mvo.md §2.3 的第 2 条口径）。
    """

    def test_penalty_suppresses_weight_drift(self, headers):
        """惩罚开大 → 权重轨迹长度显著变短。

        基准取"惩罚关闭"（κ=0、cost=0）作为对照。
        """
        free = _weights(*_run(headers, "turn_free", sizing_method="mvo",
                              mvo={"mvo_turnover_penalty": 0.0,
                                   "mvo_cost_rate": 0.0}))
        pinned = _weights(*_run(headers, "turn_pinned", sizing_method="mvo",
                                mvo={"mvo_turnover_penalty": 1.0,
                                     "mvo_cost_rate": 0.004,
                                     "mvo_turnover_horizon": 1.0}))
        len_free = _path_length(free)
        len_pinned = _path_length(pinned)
        assert len_free > 5.0, f"基准轨迹长度过小（{len_free:.3f}），用例无意义"
        assert len_pinned < 0.2 * len_free, \
            f"换手惩罚没有压住漂移：自由 {len_free:.3f} → 惩罚 {len_pinned:.3f}"

    def test_turnover_horizon_scaling(self, headers):
        """YEAR_DAY/H 折算口径：同样的成本率，H 越小 → 惩罚越强。

        c=0.001 时：H=1 → c' = 0.001×252 = 0.252（惩罚强）；
                    H=252 → c' = 0.001（几乎不起作用）。
        如果实现漏了这次折算，两边的 c' 都是 0.001 → 两条曲线重合 → 用例失败。
        """
        short_h = _weights(*_run(headers, "turn_h1", sizing_method="mvo",
                                 mvo={"mvo_turnover_penalty": 1.0,
                                      "mvo_cost_rate": 0.001,
                                      "mvo_turnover_horizon": 1.0}))
        long_h = _weights(*_run(headers, "turn_h252", sizing_method="mvo",
                                mvo={"mvo_turnover_penalty": 1.0,
                                     "mvo_cost_rate": 0.001,
                                     "mvo_turnover_horizon": 252.0}))
        len_short = _path_length(short_h)
        len_long = _path_length(long_h)
        assert len_long > 5.0, f"H=252 的基准轨迹长度过小（{len_long:.3f}）"
        assert len_short < 0.2 * len_long, \
            f"H 的折算没有生效：H=1 路径长 {len_short:.3f}，H=252 路径长 {len_long:.3f}"

    def test_penalty_is_noop_on_first_bar(self, headers):
        """首个 MVO bar 上 w_prev 恒为 0 → 惩罚项对解无影响。

        ‖w − 0‖₁ = Σw = e 是常数，与 w 无关，所以 κ 再大也不改变最优解。
        这条锁住"换手惩罚只作用在权重的变化速度上"这一实现前提。
        """
        free = _weights(*_run(headers, "turn_first_free", sizing_method="mvo",
                              mvo={"mvo_turnover_penalty": 0.0,
                                   "mvo_cost_rate": 0.0}))
        pinned = _weights(*_run(headers, "turn_first_pinned",
                                sizing_method="mvo",
                                mvo={"mvo_turnover_penalty": 50.0,
                                     "mvo_cost_rate": 0.004,
                                     "mvo_turnover_horizon": 1.0}))
        assert len(free) > 0 and len(pinned) > 0
        assert np.allclose(free[0], pinned[0], atol=1e-6), \
            f"首个 MVO bar 的权重不该受惩罚影响：{free[0]} vs {pinned[0]}"


# ============================================================
# MVO 分支算法
# ============================================================

class TestMvoBranches:
    """切换分支算法参数必须真的改变结果（否则参数没接上）。"""

    def test_kalman_vs_sample_mean(self, headers):
        """mvo_mean_method = kalman / sample 给出不同的权重。"""
        kalman = _weights(*_run(headers, "branch_kalman", sizing_method="mvo",
                                 mvo={"mvo_mean_method": "kalman"}))
        sample = _weights(*_run(headers, "branch_sample", sizing_method="mvo",
                                 mvo={"mvo_mean_method": "sample"}))
        assert len(kalman) > 10 and len(sample) > 10
        diff = np.abs(kalman - sample).max()
        assert diff > NONZERO_EPS, \
            f"卡尔曼均值与样本均值给出了几乎相同的权重（最大差 {diff:.4f}）"

    def test_ewma_shrunk_vs_sample_cov(self, headers):
        """mvo_cov_method = ewma_shrunk / sample 给出不同的权重。"""
        shrunk = _weights(*_run(headers, "branch_shrunk", sizing_method="mvo",
                                 mvo={"mvo_cov_method": "ewma_shrunk"}))
        sample = _weights(*_run(headers, "branch_cov_sample",
                                 sizing_method="mvo",
                                 mvo={"mvo_cov_method": "sample"}))
        assert len(shrunk) > 10 and len(sample) > 10
        diff = np.abs(shrunk - sample).max()
        assert diff > NONZERO_EPS, \
            f"收缩协方差与样本协方差给出了几乎相同的权重（最大差 {diff:.4f}）"
