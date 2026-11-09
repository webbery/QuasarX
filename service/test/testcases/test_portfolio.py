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


# ============================================================
# 配对交易 × 组合仓位算法
# ============================================================
#
# 上面那组用例走的是"信号策略"拓扑：4 只标的在**同一根 bar** 上一起发 BUY。
# 这种拓扑下 kelly / volatility_target / strength 三个"哑火"模型全部退化
# 成等权 —— 它们分别要读 .win_rate / .volatility / .strength，而全项目只有
# PairNode 会写 .strength，信号链路里三个 key 都拿不到，于是回退 1/N_buys；
# N_buys 恰好等于标的数 → 与等权不可区分。
#
# 配对交易是另一种拓扑：两条腿属于**同一个定仓组**（PairNode 发布
# {sym}.group_id），组内按 share（= strength 归一化，配对即 1:β）拆分，
# 组间由 sizing 模型决定总尺度。
#
# 预算分配口径（对照 src/Nodes/PortfolioNode.cpp 的 buildLegs / generatePlan）：
#   权重表覆盖**全部建仓腿**（BUY 新开多 + SELL 新开空），
#   getSymbolCapital(sym) = targetCapital * _weights[sym]
#   缺权重 = 0（不再按"有信号的标的数"兜底）
# 于是所有模型的第一步都一样：**两腿名义比 = 回归 β**。
#   equal / strength           组尺度 1 → 两腿拿满预算，按 1:β 拆
#   kelly / vol_tgt            读不到组级统计 → 组尺度 1（与上面同形）
#   mvo（min_names=1, w_max）  组尺度 = QP 权重（如 0.6），两腿同比例缩小；
#                              min_names=2 时回退等权（组尺度 1）
#
# 与修复前的关键差别：**名义比不再依赖模型**。修复前只有 strength 按 β 拆，
# equal 是 1:1（delta 不中性）、kelly/vol_tgt 是 1:2（空头腿走兜底 50%）。
# 现在组内拆分统一在 PortfolioModel::distribute 里做一次。
#
# 数据与前置条件
# --------------
# 合成配对数据由 node_test_data/generate_node_data.py 生成（两条腿各 200 根
# 日线，分子腿在 bar 80~105 被推高制造偏离）。两条腿必须在 margin_stock
# 名单里（prepare.py 预灌），否则空头腿被做空门控降级为 HOLD、整段配对仓位
# 建不起来 —— 表现为"一条信号都没有"，而不是断言失败。
#
# 观测口径
# --------
# PortfolioNode 把执行计划摘要写进 context（.plan_action / .plan_qty /
# .plan_price），DebugNode 导出成 CSV。配对策略的预算分配只发生在**开仓
# bar**上：平仓 bar 的数量来自开仓时的持仓（反向平掉），不重新分预算。
# 所以下面的断言只在开仓 bar 上做。

PAIR_SYMBOL_A = "sz.900101"   # 分子腿（价差 = P_A - β·P_B 的 A）
PAIR_SYMBOL_B = "sz.900102"   # 分母腿
PAIR_BETA_WINDOW = 30
PAIR_Z_WINDOW = 40
PAIR_ENTRY_Z = 2.0
PAIR_EXIT_Z = 0.5
PAIR_STOP_Z = 3.5
PAIR_CAPITAL = 200000
PAIR_DEBUG_LABEL = "plan"

# 多头腿权重对 β 拆分的容差（整手取整 + 四舍五入）
PAIR_BETA_WEIGHT_TOL = 0.05
# 名义比的容差不用拍脑袋的固定值，而是按**整手取整的实际误差上界**算
# （见 _ratio_error_bound）：腿 i 的名义 = 100×px_i×floor(w_i×TC/px_i/100)，
# 取整误差 ≤ 一手名义/该腿名义，名义比相对误差 ≤ 两腿之和（实测 19%~20%）。


def _build_pair_strategy(strategy_id, sizing_method, *, position_ratio=1.0,
                         max_single_pct=None, max_total_pct=None, mvo=None,
                         portfolio_allow_short=None):
    """构造 配对交易 × sizing_method 的策略图。

    拓扑（strategy_builder.pair_trading_strategy）::

        Input(A) ─┐
                  ├─► Spread ─► Function(ZScore) ─► Pair ─► Portfolio ─► Execution
        Input(B) ─┘                                   │            │
                                                       └─► Debug ───┘
                                                      （组合节点也接调试节点）

    最后一条边是本文件加的：组合节点接进 DebugNode 才能拿到 .plan_action /
    .plan_qty / .plan_price（否则只能看到 PairNode 发的 .signal / .strength）。
    """
    from strategy_builder import P, pair_trading_strategy

    strat = pair_trading_strategy(
        strategy_id, PAIR_SYMBOL_A, PAIR_SYMBOL_B,
        name=f"配对组合_{strategy_id}",
        description="配对交易 × PortfolioNode sizing_method",
        capital=PAIR_CAPITAL,
        backtest=BACKTEST_RANGE,
        source="A_hfq",
        method="rolling_regression", window=PAIR_BETA_WINDOW,
        z_window=PAIR_Z_WINDOW, entry_z=PAIR_ENTRY_Z, exit_z=PAIR_EXIT_Z,
        stop_z=PAIR_STOP_Z,
        allow_short=True,
        sizing_method=sizing_method,
        position_ratio=position_ratio,
        debug_label=PAIR_DEBUG_LABEL,
    )
    for node in strat["nodes"]:
        if node["data"]["nodeType"] != "portfolio":
            continue
        params = node["data"]["params"]
        if max_single_pct is not None:
            params["max_single_pct"] = P.number(max_single_pct)
        if max_total_pct is not None:
            params["max_total_pct"] = P.number(max_total_pct)
        for key, value in (mvo or {}).items():
            params[key] = P.of(value)

    if portfolio_allow_short is not None:
        # 必须改**根路径** margin.allowShort，不能只改节点参数：
        # StrategySubSystem 会把根路径值注入并覆盖 signal / portfolio 节点的
        # allowShort（StrategySubSystem.cpp:365-383）。`pair` 节点不在注入名单，
        # 所以 PairNode 仍按节点参数（true）发出两腿信号，而 PortfolioNode 拿到的
        # 是 false → 空头腿被丢。这正是"计划层必须 all-or-none"要覆盖的错配场景。
        strat["margin"] = {"allowShort": portfolio_allow_short}

    # 组合节点 → 调试节点
    strat["edges"].append({
        "id": "6->8", "source": "6", "target": "8",
        "sourceHandle": "6", "targetHandle": "8", "type": "default",
    })
    return strat


_PAIR_RUN_CACHE = {}


def _run_pair(headers, key, **kwargs):
    """跑一次配对回测，返回 (DataFrame, 开仓 bar 列表)。

    开仓 bar 的识别方式：顺着 plan_action / plan_qty 推持仓，
    持仓从 0 变成非 0 的那根 bar 就是开仓 bar（配对两腿同时动作）。
    """
    if key in _PAIR_RUN_CACHE:
        return _PAIR_RUN_CACHE[key]

    strategy_id = f"pf_pair_{key}"
    strategy = _build_pair_strategy(strategy_id, **kwargs)
    run_backtest_graph(strategy, headers)

    df = read_debug_csv(strategy_id, PAIR_DEBUG_LABEL)
    result = (df, _pair_open_rows(df))
    _PAIR_RUN_CACHE[key] = result
    return result


def _pair_open_rows(df):
    """挑出开仓 bar，并算出两腿的名义金额与权重。

    plan_action 的符号是**下单方向**（BUY=+1 / SELL=-1），数量恒为正：
      开多 → +1（仓位 +q）；开空 → -1（仓位 -q）；平多 → -1；平空 → +1。
    所以持仓可以顺着 action 累加还原，仓位由 0 变非 0 的那根就是开仓 bar。
    """
    act_a = df[f"{PAIR_SYMBOL_A}.plan_action"].to_numpy(dtype=float)
    act_b = df[f"{PAIR_SYMBOL_B}.plan_action"].to_numpy(dtype=float)
    qty_a = df[f"{PAIR_SYMBOL_A}.plan_qty"].to_numpy(dtype=float)
    qty_b = df[f"{PAIR_SYMBOL_B}.plan_qty"].to_numpy(dtype=float)
    px_a = df[f"{PAIR_SYMBOL_A}.plan_price"].to_numpy(dtype=float)
    px_b = df[f"{PAIR_SYMBOL_B}.plan_price"].to_numpy(dtype=float)

    pos = {PAIR_SYMBOL_A: 0.0, PAIR_SYMBOL_B: 0.0}
    rows = []
    for i in range(len(df)):
        if act_a[i] == 0 and act_b[i] == 0:
            continue
        before = dict(pos)
        if act_a[i] > 0:
            pos[PAIR_SYMBOL_A] += qty_a[i]
        elif act_a[i] < 0:
            pos[PAIR_SYMBOL_A] -= qty_a[i]
        if act_b[i] > 0:
            pos[PAIR_SYMBOL_B] += qty_b[i]
        elif act_b[i] < 0:
            pos[PAIR_SYMBOL_B] -= qty_b[i]

        opened = any(before[sym] == 0 and pos[sym] != 0 for sym in pos)
        if not opened:
            continue

        long_sym = PAIR_SYMBOL_A if pos[PAIR_SYMBOL_A] > 0 else PAIR_SYMBOL_B
        short_sym = PAIR_SYMBOL_B if long_sym == PAIR_SYMBOL_A else PAIR_SYMBOL_A
        long_not = abs(qty_a[i] * px_a[i]) if long_sym == PAIR_SYMBOL_A \
            else abs(qty_b[i] * px_b[i])
        short_not = abs(qty_b[i] * px_b[i]) if long_sym == PAIR_SYMBOL_A \
            else abs(qty_a[i] * px_a[i])
        long_px = px_a[i] if long_sym == PAIR_SYMBOL_A else px_b[i]
        short_px = px_b[i] if long_sym == PAIR_SYMBOL_A else px_a[i]
        total = long_not + short_not
        rows.append({
            "bar": i,
            "long_symbol": long_sym,
            "short_symbol": short_sym,
            "long_notional": long_not,
            "short_notional": short_not,
            "long_price": long_px,
            "short_price": short_px,
            "qty_a": qty_a[i],
            "qty_b": qty_b[i],
            "total_notional": total,
            "long_weight": long_not / total if total else 0.0,
            "ratio": long_not / short_not if short_not else float("inf"),
        })
    return rows


def _ratio_error_bound(row):
    """名义比相对期望值的**误差上界**（整手取整造成的）。

    腿 i 的名义 = 100 × px_i × floor(w_i × TC / px_i / 100)，
    取整误差上界 = 一手名义 / 该腿名义；名义比的相对误差 ≤ 两腿之和。
    """
    lot_long = 100.0 * row["long_price"]
    lot_short = 100.0 * row["short_price"]
    return (lot_long / row["long_notional"]
            + lot_short / row["short_notional"] + 0.01)


def _assert_open_bars(rows, note, min_count=5):
    """开仓 bar 数量下限 —— 少于这个数说明配对仓位压根没建起来。"""
    assert len(rows) >= min_count, (
        f"{note}: 开仓 bar 只有 {len(rows)} 个（期望 ≥ {min_count}）。"
        "配对仓位建不起来通常是两条腿不在 margin_stock 名单里"
        "（prepare.py 是否执行成功？）"
    )


def _rolling_beta(symbol_a, symbol_b, window):
    """滚动 OLS 的 β，口径与 SpreadNode 一致：窗口 [t-window, t-1]，不含当根。

    直接从 A_hfq 的 CSV 现算，不依赖 test_data_summary.json 里的参考序列 ——
    那份参考是随数据一起生成的，本地数据没重新生成时它会与 CSV 漂移
    （实测 β 差到 3.5e-3），用它做断言会在本地假失败。
    """
    import csv
    from tool import CSV_DATA_DIR

    def _closes(symbol):
        with open(CSV_DATA_DIR / f"{symbol}.csv") as f:
            return [float(row["close"]) for row in csv.DictReader(f)]

    a, b = _closes(symbol_a), _closes(symbol_b)
    out = []
    for t in range(len(a)):
        lo, hi = t - window, t
        if lo < 0:
            out.append(float("nan"))
            continue
        xs, ys = b[lo:hi], a[lo:hi]
        n = len(xs)
        mx, my = sum(xs) / n, sum(ys) / n
        cov = sum((x - mx) * (y - my) for x, y in zip(xs, ys))
        var = sum((x - mx) ** 2 for x in xs)
        out.append(cov / var if var > 1e-12 else float("nan"))
    return out


def _expected_group_weights(beta, max_single_pct=1.0, max_total_pct=1.0):
    """组等权 × share 下两腿应得的权重（复刻 distribute + applyCaps）。

    配对是一个定仓组：组尺度 = 1（equal / strength / 无统计的 kelly /
    无 σ 的 vol_tgt），组内 share 由 PairNode 的 strength 归一化得到
    （{A}=1、{B}=β）→ weight_A = 1/(1+β)、weight_B = β/(1+β)。
    applyCaps 再按单腿上限截断、总敞口超限则整体缩放。
    """
    w = {PAIR_SYMBOL_A: 1.0 / (1.0 + beta),
         PAIR_SYMBOL_B: beta / (1.0 + beta)}
    total = 0.0
    for sym in w:
        w[sym] = min(w[sym], max_single_pct)
        total += w[sym]
    if total > max_total_pct and total > 0.0:
        scale = max_total_pct / total
        for sym in w:
            w[sym] *= scale
    return w


def _expected_group_split(beta, long_symbol, max_single_pct=1.0,
                          max_total_pct=1.0):
    """返回 (多头腿权重占比, 多头/空头名义比)。

    名义比 = 权重比：notional_i = qty_i × px_i，而 qty_i 是
    floor(w_i × TC / px_i / 100) × 100 → 名义 ∝ w_i（整手取整除外）。
    """
    w = _expected_group_weights(beta, max_single_pct, max_total_pct)
    short_symbol = (PAIR_SYMBOL_B if long_symbol == PAIR_SYMBOL_A
                    else PAIR_SYMBOL_A)
    total = w[PAIR_SYMBOL_A] + w[PAIR_SYMBOL_B]
    return w[long_symbol] / total, w[long_symbol] / w[short_symbol]


# ============================================================
# Fixtures
# ============================================================

@pytest.fixture
def pair_equal(headers):
    """配对 + 等权（基线）。"""
    return _run_pair(headers, "equal", sizing_method="equal")


@pytest.fixture
def pair_strength(headers):
    """配对 + 信号强度加权。"""
    return _run_pair(headers, "strength", sizing_method="strength")


# ============================================================
# 等权基线
# ============================================================

class TestPairEqualBaseline:
    """等权在配对策略下也按对冲系数 β 拆腿（组等权 × share）。"""

    def test_equal_splits_budget_by_hedge_ratio(self, pair_equal):
        """多头腿权重 = 该腿的 share（β/(1+β) 或 1/(1+β)），不再是固定 0.5。"""
        _, rows = pair_equal
        _assert_open_bars(rows, "equal")
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            want = _expected_group_weights(beta[i])
            want_w = want[row["long_symbol"]] / (
                want[PAIR_SYMBOL_A] + want[PAIR_SYMBOL_B])
            assert abs(row["long_weight"] - want_w) < PAIR_BETA_WEIGHT_TOL, (
                f"bar {i}: 等权下多头腿权重应为 {want_w:.4f}（β={beta[i]:.4f}），"
                f"实际 {row['long_weight']:.4f}"
            )

    def test_equal_legs_notional_matches_beta(self, pair_equal):
        """两腿名义金额之比 = 回归 β（或 1/β），不再恒为 1。"""
        _, rows = pair_equal
        _assert_open_bars(rows, "equal")
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            b = beta[i]
            want = b if row["long_symbol"] == PAIR_SYMBOL_B else 1.0 / b
            rel = abs(row["ratio"] - want) / want
            assert rel <= _ratio_error_bound(row), (
                f"bar {i}: 等权下两腿名义比应为 {want:.4f}（β={b:.4f}），"
                f"实际 {row['ratio']:.4f}（相对误差 {rel:.1%} > 整手取整上界）"
            )

    def test_no_naked_leg_ever(self, pair_equal):
        """任意 bar 上都不能出现「一腿有仓、另一腿为空」的裸腿。

        配对交易不允许留裸腿 —— 一腿平了另一腿还挂着，组合就不再中性。
        这里不只是看同一 bar 的动作方向，而是顺着 plan_action / plan_qty
        还原持仓，检查每一根 bar 的两腿仓位必须同时为 0 或同时非 0。
        """
        df, _ = pair_equal
        act_a = df[f"{PAIR_SYMBOL_A}.plan_action"].to_numpy(dtype=float)
        qty_a = df[f"{PAIR_SYMBOL_A}.plan_qty"].to_numpy(dtype=float)
        act_b = df[f"{PAIR_SYMBOL_B}.plan_action"].to_numpy(dtype=float)
        qty_b = df[f"{PAIR_SYMBOL_B}.plan_qty"].to_numpy(dtype=float)
        assert len(act_a) == len(act_b)

        pos_a = pos_b = 0.0
        for i in range(len(act_a)):
            # plan_action 的符号是下单方向（BUY=+1 / SELL=-1），数量恒为正；
            # HOLD 的 action=0 不会改变持仓。
            if act_a[i] != 0 or act_b[i] != 0:
                assert act_a[i] * act_b[i] <= 0, (
                    f"bar {i}: 两腿动作应方向相反，实际 A={act_a[i]} B={act_b[i]}"
                )
            pos_a += act_a[i] * qty_a[i]
            pos_b += act_b[i] * qty_b[i]
            assert (pos_a == 0) == (pos_b == 0), (
                f"bar {i}: 出现裸腿 —— A 持仓 {pos_a:.0f}，B 持仓 {pos_b:.0f}"
            )


# ============================================================
# 两腿原子下单
# ============================================================

class TestPairAtomicOpen:
    """开仓要么两腿都下、要么都不下。

    PairNode 允许做空时会正常发出两腿信号；如果执行层（PortfolioNode）
    不允许做空，空头腿会被 generatePlan 丢掉。修复前多头腿仍会照开，
    账户留下裸多；计划层的 all-or-none 必须把整组取消。
    """

    def test_short_leg_disabled_does_not_leave_naked_long(self, headers):
        df, rows = _run_pair(headers, "strength_portfolio_noshort_pair",
                             sizing_method="strength",
                             portfolio_allow_short=False)

        assert rows == [], (
            f"PortfolioNode 禁止做空时仍然开了 {len(rows)} 次仓："
            f"{[r['bar'] for r in rows]} —— 空头腿被丢后多头腿没有一起取消"
        )
        # 排除"配对根本没发出信号"导致用例空过的可能
        sig_a = df[f"{PAIR_SYMBOL_A}.signal"].to_numpy(dtype=float)
        sig_b = df[f"{PAIR_SYMBOL_B}.signal"].to_numpy(dtype=float)
        assert ((sig_a != 0) | (sig_b != 0)).any(), (
            "PairNode 全程没有发出任何信号，用例无效"
        )


# ============================================================
# strength 加权
# ============================================================

class TestPairStrength:
    """strength 在配对策略下按对冲系数 β 分配两腿名义金额。

    PairNode 发布 {A}.strength = 1.0、{B}.strength = β；StrengthModel 把
    **两条腿**一起归一化 → weight_A = 1/(1+β)、weight_B = β/(1+β)。
    这与 backtest_pair.py 的 not_a = E/(1+β)、not_b = E·β/(1+β) 同口径
    （FINDINGS.md ①-3）。

    修复前权重表只收录 BUY 腿 → 多头腿拿满、空头腿拿一半 → 名义比恒为 2，
    与 β 无关；两腿名义合计 150% 预算。
    """

    def test_strength_weights_legs_by_beta(self, pair_strength):
        """两腿名义金额之比 = 回归 β（或 1/β，取决于哪条腿做多）。

        这是本次修复的核心断言：修复前该比值恒为 2（= 100% / 50%），
        与 β 无关；修复后应当跟着滚动回归的 β 走。

        容差不是拍脑袋定的，而是**整手取整的实际误差上界**：
        腿 i 的名义 = 100 × px_i × floor(w_i × TC / px_i / 100)，
        取整误差 ≤ 一手名义 / 该腿名义，名义比的相对误差 ≤ 两腿之和。
        （实测 3%~9%，上界 19%~20%）
        """
        _, rows = pair_strength
        _assert_open_bars(rows, "strength")
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            b = beta[i]
            want = b if row["long_symbol"] == PAIR_SYMBOL_B else 1.0 / b
            rel = abs(row["ratio"] - want) / want
            bound = _ratio_error_bound(row)
            assert rel <= bound, (
                f"bar {i}: 两腿名义比应为 {want:.4f}（回归 β={b:.4f}），"
                f"实际 {row['ratio']:.4f}（相对误差 {rel:.1%} > 整手取整上界 "
                f"{bound:.1%}）"
            )

    def test_strength_long_weight_matches_beta_split(self, pair_strength):
        """多头腿占两腿名义之和的比例 = β/(1+β)（或 1/(1+β)）。"""
        _, rows = pair_strength
        _assert_open_bars(rows, "strength")
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            w = _expected_group_weights(beta[i])
            want = w[row["long_symbol"]] / (w[PAIR_SYMBOL_A] + w[PAIR_SYMBOL_B])
            assert abs(row["long_weight"] - want) < PAIR_BETA_WEIGHT_TOL, (
                f"bar {i}: 多头腿权重应为 {want:.4f}（β={beta[i]:.4f}），"
                f"实际 {row['long_weight']:.4f}"
            )

    def test_strength_total_notional_not_overcommitted(self, pair_strength, pair_equal):
        """两腿名义合计 ≈ 预算本身，不再是 150% 超配。

        修复前多头腿拿满 targetCapital、空头腿走兜底拿 targetCapital/2
        → 合计 150%；回测下资金闸门被跳过 → 账本现金转负。
        开仓 bar 上两腿的名义合计应当等于预算本身（与等权同口径）。
        """
        _, st_rows = pair_strength
        _, eq_rows = pair_equal
        st0 = st_rows[0]["total_notional"]
        eq0 = eq_rows[0]["total_notional"]
        assert st0 < eq0 * 1.15, (
            f"首个开仓 bar 上 strength 的两腿名义合计 {st0:,.0f} 明显超过"
            f"等权的 {eq0:,.0f}（>1.15×）—— 仍是超配"
        )

    def test_strength_matches_equal_for_single_group(self, pair_equal,
                                                     pair_strength):
        """只有一个定仓组时，strength 与 equal 同形 —— 两者都是组等权 × share。

        组内拆分已经统一在 PortfolioModel::distribute 里，strength 不再有
        "跨标的按信号强度加权"的额外语义（那需要组级置信度输入，当前没有
        生产者）。所以配对组合下两者应给出完全相同的两腿股数。
        """
        _, eq_rows = pair_equal
        _, st_rows = pair_strength
        _assert_open_bars(st_rows, "strength")
        _assert_open_bars(eq_rows, "equal")

        eq_by_bar = {r["bar"]: r for r in eq_rows}
        compared = 0
        for row in st_rows:
            want = eq_by_bar.get(row["bar"])
            if want is None:
                continue
            compared += 1
            assert (row["qty_a"] == want["qty_a"]
                    and row["qty_b"] == want["qty_b"]), (
                f"bar {row['bar']}: strength 与 equal 应给出相同股数，"
                f"实际 {row['qty_a']}/{row['qty_b']} vs "
                f"{want['qty_a']}/{want['qty_b']}"
            )
        assert compared > 0, "两个模型没有任何共同的开仓 bar，无法比较"

    def test_strength_value_comes_from_spread_beta(self, pair_strength):
        """PairNode 发布的 .strength 就是价差节点回归出来的 β。

        分子腿恒为 1.0，分母腿 = 滚动 OLS 的 β（口径：窗口 [t-30, t-1]，
        不含当根 —— 与 SpreadNode 一致）。
        """
        df, rows = pair_strength
        _assert_open_bars(rows, "strength")
        strength_a = df[f"{PAIR_SYMBOL_A}.strength"].to_numpy(dtype=float)
        strength_b = df[f"{PAIR_SYMBOL_B}.strength"].to_numpy(dtype=float)
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)

        for row in rows:
            i = row["bar"]
            assert strength_a[i] == 1.0, \
                f"bar {i}: 分子腿 strength 应恒为 1.0，实际 {strength_a[i]}"
            assert abs(strength_b[i] - beta[i]) < 1e-6, \
                f"bar {i}: 分母腿 strength {strength_b[i]} != 回归 β {beta[i]}"

        # β 不是常数 —— 否则"按对冲系数加权"这句话是空的
        betas = [beta[r["bar"]] for r in rows]
        assert max(betas) - min(betas) > 1e-3, \
            f"回归 β 在开仓 bar 上几乎是常数（{betas}），用例没覆盖到变化"


# ============================================================
# 读不到输入的模型：kelly / volatility_target
# ============================================================

class TestPairDeadKeyModels:
    """kelly / volatility_target 也走**组**结构：腿间按 β 拆、总尺度由模型定。

    这两个模型分别要读 .win_rate / .volatility。组级统计的键是
    "pair.<pair_id>.xxx"（由 PairNode 发布），单腿组沿用 "{symbol}.xxx"。
    合成配对数据只有 5 笔往返、且全是盈利（avg_loss = 0）→ 凯利判定"赔率
    不可估"、目标波动率拿不到 σ → 两者都退回"每组均分" → 与 strength 同形。

    所以这里能断言的是**腿间拆分**（空头腿不再被饿死 = "支持做空组合"），
    总尺度的差异需要有"既有盈又有亏、且笔数够"的组级统计才会出现。
    """

    @pytest.mark.parametrize("method", ["kelly", "volatility_target"])
    def test_dead_key_models_split_legs_by_beta(self, headers, method):
        """两腿名义比 = 回归 β —— 空头腿不再被饿死。

        这是"凯利支持做空组合"的核心：修复前这两个模型只看 BUY 腿，空头腿
        走兜底公式 → 名义比恒为 2（与 β 无关）。
        """
        _, rows = _run_pair(headers, f"{method}_pair", sizing_method=method)
        _assert_open_bars(rows, method)
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            b = beta[i]
            want = b if row["long_symbol"] == PAIR_SYMBOL_B else 1.0 / b
            rel = abs(row["ratio"] - want) / want
            assert rel <= _ratio_error_bound(row), (
                f"bar {i}: {method} 的两腿名义比应为 {want:.4f}（β={b:.4f}），"
                f"实际 {row['ratio']:.4f}（相对误差 {rel:.1%} > 整手取整上界）"
            )

    @pytest.mark.parametrize("method", ["kelly", "volatility_target"])
    def test_dead_key_models_match_strength_without_stats(self, headers, method,
                                                          pair_strength):
        """拿不到组级统计时，它们与 strength 同形（都退回"组内按 β 拆 + 组均分"）。"""
        _, st_rows = pair_strength
        _, rows = _run_pair(headers, f"{method}_pair", sizing_method=method)
        _assert_open_bars(rows, method)
        st_by_bar = {r["bar"]: r for r in st_rows}
        compared = 0
        for got in rows:
            want = st_by_bar.get(got["bar"])
            if want is None:
                continue
            compared += 1
            assert abs(got["long_weight"] - want["long_weight"]) < 1e-9, \
                f"bar {got['bar']}: {method} 的权重 {got['long_weight']:.4f} " \
                f"与 strength 的 {want['long_weight']:.4f} 不一致"
        assert compared > 0, "两个模型没有任何共同的 bar，无法比较"

    @pytest.mark.parametrize("method", ["kelly", "volatility_target"])
    def test_dead_key_models_no_naked_leg(self, headers, method):
        """组结构不能让任一条腿落单（组权重展开后两腿都拿资金）。"""
        df, rows = _run_pair(headers, f"{method}_pair", sizing_method=method)
        _assert_open_bars(rows, method)
        act_a = df[f"{PAIR_SYMBOL_A}.plan_action"].to_numpy(dtype=float)
        qty_a = df[f"{PAIR_SYMBOL_A}.plan_qty"].to_numpy(dtype=float)
        act_b = df[f"{PAIR_SYMBOL_B}.plan_action"].to_numpy(dtype=float)
        qty_b = df[f"{PAIR_SYMBOL_B}.plan_qty"].to_numpy(dtype=float)
        pos_a = np.cumsum(np.where(act_a > 0, qty_a, np.where(act_a < 0, -qty_a, 0.0)))
        pos_b = np.cumsum(np.where(act_b > 0, qty_b, np.where(act_b < 0, -qty_b, 0.0)))
        for i in range(len(df)):
            assert (pos_a[i] == 0) == (pos_b[i] == 0), (
                f"bar {i}: {method} 出现裸腿（A={pos_a[i]:.0f}, B={pos_b[i]:.0f}）"
            )


# ============================================================
# MVO 回退
# ============================================================

class TestPairMvo:
    """MVO 在配对下的两种落法。"""

    def test_mvo_falls_back_to_equal(self, headers, pair_equal):
        """候选 = 多头腿；配对每根 bar 只有 1 条腿做多，候选数 1 <
        min_names=2 → 告警并回退等权（组尺度 1，组内仍按 β 拆）。
        """
        _, eq_rows = pair_equal
        _, rows = _run_pair(headers, "mvo_pair", sizing_method="mvo")
        _assert_open_bars(rows, "mvo")
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            want = _expected_group_weights(beta[i])
            want_w = want[row["long_symbol"]] / (
                want[PAIR_SYMBOL_A] + want[PAIR_SYMBOL_B])
            assert abs(row["long_weight"] - want_w) < PAIR_BETA_WEIGHT_TOL, (
                f"bar {i}: MVO 回退等权时多头腿权重应为 {want_w:.4f}，"
                f"实际 {row['long_weight']:.4f}"
            )

    def test_mvo_min_names_one_actually_solves(self, headers, pair_equal):
        """把 min_names 压到 1 → MVO 真正求解，单候选拿到 w_max。

        单候选时 QP 退化成"给这一个名字多少"，被 mvo_max_weight（默认 0.60）
        卡住 → 组的尺度 = 0.60（QP 权重就是组预算），两腿按 β 同比例缩小。
        所以可观测的差别不在腿间比例（恒为 β），而在**总名义**：
        首根开仓 bar 的总名义应约为等权的 0.60 倍。
        """
        _, eq_rows = pair_equal
        _, rows = _run_pair(headers, "mvo_min1_pair", sizing_method="mvo",
                            mvo={"mvo_min_names": 1})
        _assert_open_bars(rows, "mvo(min_names=1)")
        _assert_open_bars(eq_rows, "equal")
        ratio = rows[0]["total_notional"] / eq_rows[0]["total_notional"]
        assert 0.50 < ratio < 0.70, (
            f"首根开仓 bar：MVO(min_names=1) 的总名义应为等权的 ≈0.60 倍"
            f"（w_max=0.60 就是组预算），实际 {ratio:.3f}"
        )

    def test_mvo_max_weight_controls_single_candidate(self, headers, pair_equal):
        """单候选时组尺度由 mvo_max_weight 决定：放到 1.0 → 与等权同形。

        QP 权重 = 组预算，w_max=1.0 → 组尺度 1.0，两腿按 β 分满预算，
        与 equal 完全一致（但权重来自 QP，不是兜底）。
        """
        _, eq_rows = pair_equal
        _, rows = _run_pair(headers, "mvo_min1_maxw1_pair", sizing_method="mvo",
                            mvo={"mvo_min_names": 1, "mvo_max_weight": 1.0})
        _assert_open_bars(rows, "mvo(min_names=1,max_weight=1.0)")
        _assert_open_bars(eq_rows, "equal")
        ratio = rows[0]["total_notional"] / eq_rows[0]["total_notional"]
        assert 0.90 < ratio < 1.10, (
            f"首根开仓 bar：w_max=1.0 时组尺度应为 1.0，总名义应≈等权，"
            f"实际 {ratio:.3f}"
        )


# ============================================================
# 上限参数
# ============================================================

class TestPairSizingCaps:
    """max_single_pct / max_total_pct 作用在**权重表里的标的**上。

    strength 的权重表现在**包含两条腿** → 上限对两腿同时生效。
    修复前空头腿不在权重表里（走兜底 targetCapital/count），不受上限约束。
    """

    def test_max_single_pct_caps_strength_both_legs(self, headers):
        """strength + max_single_pct=0.5 → 两腿都被截到 ≤0.5，β 比例被压平。

        β≈1.25 时 w_B = β/(1+β) ≈ 0.556 > 0.5 → 被截到 0.5，
        而 w_A = 1/(1+β) ≈ 0.444 不受影响 → 名义比从 β 变成 ≈1.13。
        这条同时记录了"单标的上限会破坏 β 比例"这个副作用
        （FINDINGS.md ①-3 的坑 #3）。
        """
        _, rows = _run_pair(headers, "strength_cap50_pair", sizing_method="strength",
                            max_single_pct=0.5)
        _assert_open_bars(rows, "strength+cap0.5")
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            want_w, want_ratio = _expected_group_split(
                beta[i], row["long_symbol"], max_single_pct=0.5)
            assert abs(row["long_weight"] - want_w) < PAIR_BETA_WEIGHT_TOL, (
                f"bar {i}: 上限 0.5 之下多头腿权重应为 {want_w:.4f}，"
                f"实际 {row['long_weight']:.4f}"
            )
            rel = abs(row["ratio"] - want_ratio) / want_ratio
            assert rel <= _ratio_error_bound(row), (
                f"bar {i}: 上限 0.5 之下名义比应为 {want_ratio:.4f}，"
                f"实际 {row['ratio']:.4f}（相对误差 {rel:.1%} > 整手取整上界）"
            )

    def test_max_single_pct_small_makes_legs_equal(self, headers):
        """strength + max_single_pct=0.3 → 两腿都被截到 0.3 → 名义比 = 1。

        修复前空头腿不在权重表里、走 50% 兜底 → 多头 30% 比空头 50% 小。
        现在两腿都在表里且都被截断 → 一样多，比值回到 1。

        容差放宽：两腿都被截到 30% 预算时，一手占该腿名义的比例变大
        （实测 |w-0.5| ≤ 0.030、|ratio-1| ≤ 0.116），仍用整手取整误差上界。
        """
        _, rows = _run_pair(headers, "strength_cap30_pair", sizing_method="strength",
                            max_single_pct=0.3)
        _assert_open_bars(rows, "strength+cap0.3")
        for row in rows:
            assert abs(row["long_weight"] - 0.5) < 0.05, (
                f"bar {row['bar']}: 两腿都被截到 0.3 后多头腿权重应为 0.50，"
                f"实际 {row['long_weight']:.4f}"
            )
            rel = abs(row["ratio"] - 1.0)
            assert rel <= _ratio_error_bound(row), (
                f"bar {row['bar']}: 两腿都被截断后名义比应为 ≈1，"
                f"实际 {row['ratio']:.4f}（偏差 {rel:.1%} > 整手取整上界）"
            )

    def test_max_total_pct_caps_strength(self, headers):
        """strength + max_total_pct=0.5 → 权重表整体缩到 50%，但**比例不变**。

        总上限对两腿等比缩放 → 名义比仍是 β、多头腿占比仍是 β/(1+β)；
        变的只是总预算（与 strength 不加上限时同形）。
        """
        _, rows = _run_pair(headers, "strength_total50_pair", sizing_method="strength",
                            max_total_pct=0.5)
        _assert_open_bars(rows, "strength+max_total 0.5")
        beta = _rolling_beta(PAIR_SYMBOL_A, PAIR_SYMBOL_B, PAIR_BETA_WINDOW)
        for row in rows:
            i = row["bar"]
            want_w, want_ratio = _expected_group_split(
                beta[i], row["long_symbol"], max_total_pct=0.5)
            assert abs(row["long_weight"] - want_w) < PAIR_BETA_WEIGHT_TOL, (
                f"bar {i}: 总上限 0.5 之下多头腿权重应为 {want_w:.4f}，"
                f"实际 {row['long_weight']:.4f}"
            )
            rel = abs(row["ratio"] - want_ratio) / want_ratio
            assert rel <= _ratio_error_bound(row), (
                f"bar {i}: 总上限是等比缩放，名义比应仍是 {want_ratio:.4f}，"
                f"实际 {row['ratio']:.4f}（相对误差 {rel:.1%} > 整手取整上界）"
            )

    def test_equal_respects_caps(self, headers):
        """等权也走 applyCaps —— 上限对它有和 strength 一样的效果。

        改造前 EqualModel 返回空权重表，预算走 perSymbolCapital，上限根本
        不生效；现在 equal 与 strength 同形（组等权 × share），两腿都被
        max_single_pct=0.5 截断。
        """
        _, base = _run_pair(headers, "strength_cap50_pair",
                            sizing_method="strength", max_single_pct=0.5)
        _, rows = _run_pair(headers, "equal_cap50_pair", sizing_method="equal",
                            max_single_pct=0.5)
        _assert_open_bars(rows, "equal+cap0.5")
        _assert_open_bars(base, "strength+cap0.5")
        base_by_bar = {r["bar"]: r for r in base}
        compared = 0
        for row in rows:
            want = base_by_bar.get(row["bar"])
            if want is None:
                continue
            compared += 1
            assert abs(row["long_weight"] - want["long_weight"]) < 1e-9, \
                f"bar {row['bar']}: equal 与 strength 在 cap0.5 下应同形" \
                f"（{row['long_weight']:.4f} vs {want['long_weight']:.4f}）"
        assert compared > 0, "两个配置没有共同的开仓 bar"


# ============================================================
# 仓位比例
# ============================================================

class TestPairPositionRatio:
    """positionRatio 线性缩放整笔预算，且不改变两腿之间的比例结构。"""

    def test_position_ratio_halves_budget(self, headers, pair_equal):
        """positionRatio=0.5 → 每根开仓 bar 的总名义金额约减半。"""
        _, eq_rows = pair_equal
        _, rows = _run_pair(headers, "equal_ratio05_pair", sizing_method="equal",
                            position_ratio=0.5)
        _assert_open_bars(rows, "equal+ratio0.5")
        for got, want in zip(rows, eq_rows):
            ratio = got["total_notional"] / want["total_notional"]
            assert 0.35 < ratio < 0.65, (
                f"bar {got['bar']}: positionRatio=0.5 时总名义应为原来的一半左右，"
                f"实际比例 {ratio:.3f}"
            )

    def test_position_ratio_keeps_weight_structure(self, headers, pair_equal):
        """positionRatio 只缩放总额，不改变两腿的相对分配（仍是 1:β）。

        容差放宽到 0.06：预算减半后整手取整的相对误差变大
        （20 万预算时一手误差 ≈0.4%，10 万预算时 ≈0.9%）。
        """
        _, eq_rows = pair_equal
        _, rows = _run_pair(headers, "equal_ratio05_pair", sizing_method="equal",
                            position_ratio=0.5)
        _assert_open_bars(rows, "equal+ratio0.5")
        for got, want in zip(rows, eq_rows):
            assert abs(got["long_weight"] - want["long_weight"]) < 0.06, (
                f"bar {got['bar']}: positionRatio=0.5 下多头腿权重 {got['long_weight']:.4f} "
                f"与全仓时的 {want['long_weight']:.4f} 偏离过大"
            )
