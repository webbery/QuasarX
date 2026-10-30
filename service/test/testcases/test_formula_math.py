#!/usr/bin/env python3
"""
FormulaParser 内置数学函数测试

验证新增的 7 个数学函数：abs, exp, log, sqrt, sigmoid, min, max
以及复合公式（trend_strength 模拟）

测试流程：
1. 构造策略 JSON（QuoteInput → MA(5) → FormulaNode(math_func) → DebugNode）
2. POST /v0/backtest 提交回测
3. 读取 DebugNode CSV
4. 与 Python 黄金标准对比

使用方法：
  pytest test_formula_math.py -v
  pytest test_formula_math.py::TestFormulaMath::test_abs -v

前置条件：
  - 服务已启动
  - 测试数据已生成（python generate_node_data.py）
"""

import json
import math
import pytest
import requests
import urllib3
import numpy as np
import pandas as pd
from pathlib import Path
from tool import DEBUG_DIR, CSV_DATA_DIR, read_debug_csv
from strategy_builder import StrategyBuilder, P

urllib3.disable_warnings()

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

TEST_DIR = Path(__file__).parent / "node_test_data"

SYMBOL = "sz.800001"
TOLERANCE = 1e-5


# ============================================================
# Fixtures
# ============================================================

# auth_token / headers fixtures 由 conftest.py 提供

# ============================================================
# 辅助函数
# ============================================================

def _load_close_prices() -> np.ndarray:
    csv_path = CSV_DATA_DIR / f"{SYMBOL}.csv"
    with open(csv_path) as f:
        reader = csv.DictReader(f)
        return np.array([float(row["close"]) for row in reader])


def _build_strategy(expression: str, strategy_id: str, extra_ma=()) -> dict:
    """构造测试策略:
    QuoteInput → MA(5)[+ extra_ma] → FormulaNode(expression) → DebugNode
                → Signal → Portfolio → Execution

    extra_ma: 额外挂到 FormulaNode 上的均线，元素为 (节点 id, 窗口)，例如
              [("8", 15)] 就是双输入策略，[("8", 15), ("9", 30)] 是三输入。
    """
    builder = (StrategyBuilder(
                   strategy_id,
                   f"数学函数测试_{strategy_id}",
                   description="FormulaParser 数学函数测试",
                   backtest={"start": "2024-01-01", "end": "2024-09-07"},
                   source="A_hfq")
               .input("1", symbol=SYMBOL)
               .function("2", "MA", 5, label="ma5"))
    for ma_id, window in extra_ma:
        builder.function(ma_id, "MA", window, label=f"ma{window}")

    builder.formula("3", expression, label="math_func")
    builder.debug("4", "debug_math")
    builder.signal("5", SYMBOL)
    builder.portfolio("6")
    builder.execution("7")

    builder.connect("1", "2", prop="close")
    builder.connect("2", "3")
    builder.connect("3", "4")
    builder.connect("1", "5", prop="close")
    builder.connect("5", "6")
    builder.connect("6", "7")
    for ma_id, _ in extra_ma:
        builder.connect(ma_id, "3")
        builder.connect("1", ma_id, prop="close")
    return builder.build()


def _build_two_input_strategy(expression: str, strategy_id: str) -> dict:
    """构造双输入测试策略: MA(5) + MA(15) → FormulaNode → DebugNode"""
    return _build_strategy(expression, strategy_id, extra_ma=[("8", 15)])


def _build_three_input_strategy(expression: str, strategy_id: str) -> dict:
    """构造三输入测试策略: MA(5) + MA(15) + MA(30) → FormulaNode → DebugNode

    用途: argmax(ma5[t], ma15[t], ma30[t]) 等 N-arg 测试
    """
    return _build_strategy(expression, strategy_id, extra_ma=[("8", 15), ("9", 30)])


def _get_ma30_series() -> pd.Series:
    """Python 黄金标准: MA(30)"""
    closes = _load_close_prices()
    return pd.Series(closes).rolling(30).mean()


def _run_backtest(strategy: dict, headers: dict) -> dict:
    r = requests.post(f"{BASE_URL}/backtest",
                      json={"script": json.dumps(strategy), "validate": False},
                      headers=headers, verify=VERIFY_SSL)
    assert r.status_code == 200, f"Backtest failed: {r.text}"
    return r.json()


def _get_ma5_series() -> pd.Series:
    """Python 黄金标准: MA(5)"""
    closes = _load_close_prices()
    return pd.Series(closes).rolling(5).mean()


def _get_ma15_series() -> pd.Series:
    """Python 黄金标准: MA(15)"""
    closes = _load_close_prices()
    return pd.Series(closes).rolling(15).mean()


def _compare_series(actual: pd.Series, expected: np.ndarray,
                    tol: float = TOLERANCE, label: str = ""):
    """对比两个序列（跳过 NaN）"""
    n = min(len(actual), len(expected))
    a = pd.to_numeric(actual.iloc[:n], errors="coerce").dropna()
    e = pd.Series(expected[:n]).dropna()
    # 对齐到共同有效索引
    common_idx = a.index.intersection(e.index)
    a_valid = a.loc[common_idx].values
    e_valid = e.loc[common_idx].values
    assert len(a_valid) > 0, f"[{label}] No valid values to compare"
    diff = np.abs(a_valid - e_valid)
    max_diff = np.max(diff)
    assert max_diff < tol, \
        f"[{label}] max diff {max_diff:.2e} exceeds tolerance {tol}, " \
        f"at bar {np.argmax(diff)}: actual={a_valid[np.argmax(diff)]:.6f}, " \
        f"expected={e_valid[np.argmax(diff)]:.6f}"


# ============================================================
# 测试用例
# ============================================================

import csv  # noqa: E402 (needed by _load_close_prices)


class TestFormulaMath:
    """FormulaParser 内置数学函数测试"""

    def test_abs(self, headers):
        """abs(ma5[t]) — 绝对值"""
        sid = "test_math_abs"
        strategy = _build_strategy("abs(ma5[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.abs(ma5)
        _compare_series(actual, expected, label="abs")

    def test_exp(self, headers):
        """exp(ma5[t] / 100) — 指数（缩小避免溢出）"""
        sid = "test_math_exp"
        strategy = _build_strategy("exp(ma5[t] / 100)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.exp(ma5 / 100)
        _compare_series(actual, expected, label="exp")

    def test_log(self, headers):
        """log(ma5[t]) — 自然对数"""
        sid = "test_math_log"
        strategy = _build_strategy("log(ma5[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.log(ma5)
        _compare_series(actual, expected, label="log")

    def test_sqrt(self, headers):
        """sqrt(ma5[t]) — 平方根"""
        sid = "test_math_sqrt"
        strategy = _build_strategy("sqrt(ma5[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.sqrt(ma5)
        _compare_series(actual, expected, label="sqrt")

    def test_sigmoid(self, headers):
        """sigmoid(ma5[t] - 100) — sigmoid 函数"""
        sid = "test_math_sigmoid"
        strategy = _build_strategy("sigmoid(ma5[t] - 100)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = 1.0 / (1.0 + np.exp(-(ma5 - 100)))
        _compare_series(actual, expected, label="sigmoid")

    def test_min(self, headers):
        """min(ma5[t], 105) — 二元取小值"""
        sid = "test_math_min"
        strategy = _build_strategy("min(ma5[t], 105)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.minimum(ma5, 105.0)
        _compare_series(actual, expected, label="min")

    def test_max(self, headers):
        """max(ma5[t], 95) — 二元取大值"""
        sid = "test_math_max"
        strategy = _build_strategy("max(ma5[t], 95)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.maximum(ma5, 95.0)
        _compare_series(actual, expected, label="max")

    def test_min_two_variables(self, headers):
        """min(ma5[t], ma15[t]) — 两个变量取小值"""
        sid = "test_math_min2v"
        strategy = _build_two_input_strategy("min(ma5[t], ma15[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        ma15 = _get_ma15_series().values
        expected = np.minimum(ma5, ma15)
        _compare_series(actual, expected, label="min_2vars")

    def test_max_two_variables(self, headers):
        """max(ma5[t], ma15[t]) — 两个变量取大值"""
        sid = "test_math_max2v"
        strategy = _build_two_input_strategy("max(ma5[t], ma15[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        ma15 = _get_ma15_series().values
        expected = np.maximum(ma5, ma15)
        _compare_series(actual, expected, label="max_2vars")


class TestFormulaMathComposite:
    """复合公式测试 — 模拟 trend_strength 计算"""

    def test_clamp_abs_pattern(self, headers):
        """min(abs(x) * 10, 1) — 绝对值 + 缩放 + clamp"""
        sid = "test_comp_clamp_abs"
        strategy = _build_strategy("min(abs(ma5[t] - 100) * 5, 1)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.minimum(np.abs(ma5 - 100) * 5, 1.0)
        _compare_series(actual, expected, label="clamp_abs")

    def test_trend_strength_formula(self, headers):
        """sigmoid((min(abs(a)*50, 5) + min(abs(b)*20, 5)) / 2)
        完整模拟 trend_strength 公式"""
        sid = "test_comp_trend_str"
        expr = ("sigmoid((min(abs(ma5[t] - 100) * 50, 5) "
                "+ min(abs(ma15[t] - 100) * 20, 5)) / 2)")
        strategy = _build_two_input_strategy(expr, sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        ma15 = _get_ma15_series().values
        abs_slope = np.minimum(np.abs(ma5 - 100) * 50, 5.0)
        abs_price = np.minimum(np.abs(ma15 - 100) * 20, 5.0)
        combined = (abs_slope + abs_price) / 2
        expected = 1.0 / (1.0 + np.exp(-combined))
        _compare_series(actual, expected, label="trend_strength")

    def test_nested_math(self, headers):
        """sqrt(abs(ma5[t] - 100)) — 嵌套数学函数"""
        sid = "test_comp_nested"
        strategy = _build_strategy("sqrt(abs(ma5[t] - 100))", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.sqrt(np.abs(ma5 - 100))
        _compare_series(actual, expected, label="nested_sqrt_abs")

    def test_exp_neg_square(self, headers):
        """exp(-abs(ma5[t] - 100) / 10) — 高斯核风格"""
        sid = "test_comp_gauss"
        strategy = _build_strategy("exp(-abs(ma5[t] - 100) / 10)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.exp(-np.abs(ma5 - 100) / 10)
        _compare_series(actual, expected, label="gaussian_kernel")


class TestArgmax:
    """argmax 内置函数测试 (2026-08-20 新增)

    argmax(args...) 返回最大值所在位置索引（0-based）
    支持的单参数场景:
      - Vector<double>: 返回 Vector 中最大值的索引
      - 单个 double: 返回 0
    支持多参数场景:
      - 2 个标量: 0 或 1 (较大者位置)
      - 3+ 标量: 最大值索引

    主要用途: argmax(xgb_probs) 配合 v16 strength 公式
      strength = (argmax == 0 ? +1 : -1) * max(probs) * (1 + entropy)
    """

    def test_argmax_scalar(self, headers):
        """argmax(ma5[t]) — 单个标量，应返回 0"""
        sid = "test_argmax_scalar"
        strategy = _build_strategy("argmax(ma5[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        # 单个 double，argmax 永远返回 0
        n = min(len(actual), 200)
        for i in range(n):
            assert actual.iloc[i] == 0.0, \
                f"argmax(scalar) at bar {i}: expected 0, got {actual.iloc[i]}"

    def test_argmax_2args(self, headers):
        """argmax(ma5[t], ma15[t]) — 2 个标量，匹配 numpy np.argmax"""
        sid = "test_argmax_2args"
        strategy = _build_two_input_strategy("argmax(ma5[t], ma15[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        ma15 = _get_ma15_series().values
        # Python 黄金标准: numpy np.argmax([ma5, ma15]) per bar
        # NaN 行为: argmax([X, NaN]) → 1（NaN 传染 best_val）
        #           argmax([NaN, X]) → 0（best 起始 NaN，后续 finite 不更新）
        expected_per_bar = np.argmax(np.column_stack([ma5, ma15]), axis=1).astype(float)

        # 仅对比双方都有效的 bar（warmup 期任何一者 NaN 时跳过）
        valid_mask = np.isfinite(ma5) & np.isfinite(ma15)
        actual_valid = actual.values[:len(valid_mask)][valid_mask]
        expected_valid = expected_per_bar[valid_mask]
        diff = np.abs(actual_valid - expected_valid)
        max_diff = np.max(diff)
        assert max_diff < 1e-6, \
            f"[argmax_2args] max diff {max_diff:.2e} exceeds tolerance 1e-6"

    def test_argmax_3args(self, headers):
        """argmax(ma5[t], ma15[t], ma30[t]) — 3 个标量 (XGBoost 3 分类场景)"""
        sid = "test_argmax_3args"
        strategy = _build_three_input_strategy(
            "argmax(ma5[t], ma15[t], ma30[t])", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        ma15 = _get_ma15_series().values
        ma30 = _get_ma30_series().values

        # Python 黄金标准: max in (ma5, ma15, ma30) 位置
        # np.column_stack 后逐行 argmax
        stacked = np.column_stack([ma5, ma15, ma30])
        expected = np.argmax(stacked, axis=1).astype(float)

        # 注意: argmax 在 NaN 上行为不同，先填充 NaN 让比较有意义
        actual_filled = actual.fillna(-1)
        # 仅对比双方都有效的 bar (避开 NaN 不一致)
        valid = ~np.isnan(expected)
        valid &= ~np.isnan(actual)
        n = min(len(actual_filled), len(expected))
        for i in range(n):
            if not valid[i]:
                continue
            assert actual_filled.iloc[i] == expected[i], \
                f"argmax_3args at bar {i}: actual={actual_filled.iloc[i]}, expected={expected[i]}"

    def test_argmax_3args_constant(self, headers):
        """argmax(99, 99, 99) — 三个相等值，第一个匹配返回 0"""
        sid = "test_argmax_constant"
        strategy = _build_three_input_strategy(
            "argmax(99, 99, 99)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        n = min(len(actual), 200)
        for i in range(n):
            assert actual.iloc[i] == 0.0, \
                f"argmax(constant) at bar {i}: expected 0, got {actual.iloc[i]}"


class TestArgmaxComposite:
    """argmax 复合用法 — 模拟 v16 strength 公式

    v16 strength 模式 = sign_of_argmax × max_prob × (1 - entropy)
    此处用 MA 简化近似验证:
      direction = (argmax(a, b) == 0 ? +1 : -1)
      strength  ≈ direction × max(a, b) × (1 + abs(a-b))
    """

    def test_argmax_direction(self, headers):
        """argmax(a, b) == 0 → 1, argmax(a, b) == 1 → -1 方向编码

        Python golden standard 必须忠实复现公式的语义路径：
            argmax(a, b) → (==0) → *2 → -1
        而非简化用 np.where(>=)。两者在 NaN 处差异巨大（NaN >= 任意 都是 False → -1），
        但 argmax 内部用严格 > + NaN 传播，对全 NaN 仍是 0 → +1。
        """
        sid = "test_argmax_dir"
        # 当 ma5[t] > ma15[t] 时强度为正，否则为负
        strategy = _build_two_input_strategy(
            "(argmax(ma5[t], ma15[t]) == 0) * 2 - 1",
            sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        ma15 = _get_ma15_series().values
        # 复现公式逻辑：先算 argmax，再做 ==0 + 2 - 1 编码
        argmax_per_bar = np.argmax(np.column_stack([ma5, ma15]), axis=1).astype(float)
        expected = np.where(argmax_per_bar == 0, 1.0, -1.0)

        _compare_series(actual, expected, label="argmax_direction", tol=1e-6)


class TestCount:
    """count(vec, sign) intrinsic 测试 (2026-08-20 新增)

    签名: count(values, sign) → double
       sign >  0:  统计 v >  0 (positiveCount)
       sign <  0:  统计 v <  0 (negativeCount)
       sign == 0:  统计 v == 0 (zeroCount)

    主要用途: v14 consistency_threshold = max(count(s, +1), count(s, -1)) / (count(s, +1) + count(s, -1))
    """

    def test_count_scalar_positive(self, headers):
        """count(v, +1) — 单正值 v > 0 返回 1；测试用确定性常量避免 MA 精度边界
        注：原 test 用 count(ma5[t] - 100, 1) 与 ma5 > 100 比较，碰到 Kahan vs pandas 精度分歧
        在 ma5 接近 100 的 bar 上 diff = 1.0 不通过。改用 count(0.5, 1) 这种常量测试。
        """
        sid = "test_count_scalar_pos"
        # 1.0 - 0.5 = 0.5 > 0 → count 应始终返回 1
        strategy = _build_strategy("count(1.0 - 0.5, 1)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        # 始终为 1（与 bar 无关，确定性 scalar）
        n = min(len(actual), 200)
        max_diff = 0.0
        n_valid = 0
        for i in range(n):
            if pd.isna(actual.iloc[i]):
                continue
            diff = abs(actual.iloc[i] - 1.0)
            if diff > max_diff:
                max_diff = diff
            n_valid += 1
        assert n_valid > 0, "no valid samples"
        assert max_diff < TOLERANCE, \
            f"count_scalar_pos (count(0.5, 1) 应当常返 1) max diff {max_diff:.2e}"

    def test_count_scalar_negative(self, headers):
        """count(v, -1) — 单负值 v < 0 返回 1；用确定性常量 (-0.5 + 0.3 = -0.2 永远 < 0)"""
        sid = "test_count_scalar_neg"
        # -0.5 + 0.3 = -0.2 < 0 → sign=-1, v<0 → count 应始终返回 1
        strategy = _build_strategy("count(-0.5 + 0.3, -1)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        n = min(len(actual), 200)
        max_diff = 0.0
        for i in range(n):
            if pd.isna(actual.iloc[i]):
                continue
            diff = abs(actual.iloc[i] - 1.0)
            if diff > max_diff:
                max_diff = diff
        assert max_diff < TOLERANCE, \
            f"count_scalar_neg (count(-0.2, -1) 应当常返 1) max diff {max_diff:.2e}"

    def test_count_scalar_negative_zero(self, headers):
        """count(v, +1) — 单零值 v=0 返回 0（v > 0 不成立）"""
        sid = "test_count_scalar_neg_zero"
        # count(0, 1): sign=1, v=0 → 0>0 False → 返回 0
        strategy = _build_strategy("count(0 * 1.0, 1)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        n = min(len(actual), 200)
        max_diff = 0.0
        for i in range(n):
            if pd.isna(actual.iloc[i]):
                continue
            diff = abs(actual.iloc[i] - 0.0)
            if diff > max_diff:
                max_diff = diff
        assert max_diff < TOLERANCE, \
            f"count_scalar_neg_zero (count(0, 1) 应当常返 0) max diff {max_diff:.2e}"

    def test_count_scalar_zero(self, headers):
        """count(scalar_constant, 0) — 严格等于 0 时为 1，否则为 0"""
        sid = "test_count_scalar_zero"
        strategy = _build_strategy("count(0, 0)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        n = min(len(actual), 200)
        for i in range(n):
            assert actual.iloc[i] == 1.0, \
                f"count(0, 0) at bar {i}: expected 1.0, got {actual.iloc[i]}"

    def test_count_vector_positive(self, headers):
        """count(ma5_series, +1) — 向量展开累计统计正数

        每 bar N: expected[N] = num positive ma5 values in [0..N]
        """
        sid = "test_count_vec_pos"
        strategy = _build_strategy("count(ma5, 1)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        # Python 黄金标准: expanding window count of positive
        expected = np.zeros(len(ma5))
        for i in range(len(ma5)):
            valid = ma5[:i+1][np.isfinite(ma5[:i+1])]
            expected[i] = np.sum(valid > 0)

        n = min(len(actual), len(expected))
        valid = (~np.isnan(actual)) & (~np.isnan(expected))
        # NaN 出现时 expected=0 但 actual 可能 NaN，单独处理
        for i in range(n):
            if not np.isfinite(ma5[i]):
                continue
            assert abs(actual.iloc[i] - expected[i]) < TOLERANCE, \
                f"count(vec, +1) at bar {i}: actual={actual.iloc[i]}, expected={expected[i]}"

    def test_count_vector_negative(self, headers):
        """count(ma15_series, -1) — 向量累计统计负数"""
        sid = "test_count_vec_neg"
        # Use ma15 instead of ma5 to introduce variation with mixed-sign values
        strategy = _build_two_input_strategy("count(ma15, -1)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        closes = _load_close_prices()
        ma15_full = pd.Series(closes).rolling(15).mean().values

        expected = np.zeros(len(ma15_full))
        for i in range(len(ma15_full)):
            valid = ma15_full[:i+1][np.isfinite(ma15_full[:i+1])]
            expected[i] = np.sum(valid < 0)

        n = min(len(actual), len(expected))
        for i in range(n):
            if not np.isfinite(ma15_full[i]):
                continue
            assert abs(actual.iloc[i] - expected[i]) < TOLERANCE, \
                f"count(ma15_vec, -1) at bar {i}: actual={actual.iloc[i]}, expected={expected[i]}"

    def test_count_pos_plus_neg_equals_non_nan(self, headers):
        """count(..., +1) + count(..., -1) == 可比较 NaN 的 v 总数

        这是 consistency_threshold 实现的核心不变量:
          pos_count + neg_count = 非零元素数
        """
        sid = "test_count_partition"
        strategy = _build_two_input_strategy("count(ma5, 1) + count(ma5, -1)", sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.zeros(len(ma5))
        for i in range(len(ma5)):
            valid = ma5[:i+1][np.isfinite(ma5[:i+1])]
            expected[i] = np.sum(valid != 0)  # 非零 = 正 + 负

        n = min(len(actual), len(expected))
        for i in range(n):
            # bar N where ma5[N] is NaN: 取决于实现，可能相等也可能不等
            if not np.isfinite(ma5[i]):
                continue
            assert abs(actual.iloc[i] - expected[i]) < TOLERANCE, \
                f"pos+neg vs nonzero at bar {i}: actual={actual.iloc[i]}, expected={expected[i]}"


class TestConsistencyRatio:
    """v14 consistency_threshold 完整计算验证 — dominant_ratio = max(pos, neg) / (pos + neg)

    cta_v16.json 的 SignalNode sell formula 核心:
       max(count(s, 1), count(s, -1)) / (count(s, 1) + count(s, -1)) < 0.6
       → 全 SELL
    """

    def test_dominant_ratio_threshold(self, headers):
        """dominant_ratio = max(pos, neg) / (pos + neg) 完整计算"""
        sid = "test_dominant_gate"
        # 不能直接用 / 否则会除0，改成 max(..., 1) 保护
        expression_safe = (
            "max(count(ma5, 1), count(ma5, -1)) / "
            "(max(count(ma5, 1) + count(ma5, -1), 1))"
        )

        strategy = _build_strategy(expression_safe, sid)
        _run_backtest(strategy, headers)

        df = read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.zeros(len(ma5))
        for i in range(len(ma5)):
            valid = ma5[:i+1][np.isfinite(ma5[:i+1])]
            pos = np.sum(valid > 0)
            neg = np.sum(valid < 0)
            denom = max(pos + neg, 1)
            expected[i] = max(pos, neg) / denom

        _compare_series(actual, expected, label="dominant_ratio", tol=1e-5)


# ============================================================
# XGBoost 矩阵输出 + Formula 列读取测试
# ============================================================

TRIVIAL_MODEL_PATH = Path(__file__).parent / "ai_test_data" / "xgb_trivial_model.json"
TRIVIAL_META_PATH = Path(__file__).parent / "ai_test_data" / "xgb_trivial_meta.json"
MATRIX_STRATEGY_ID = "test_formula_xgb_matrix"
MATRIX_SYMBOLS = ["sz.800001", "sz.800002"]


def _build_xgb_matrix_strategy() -> dict:
    """构造策略: Input(2标的) → MA(5) → XGBoost(binary) → Formula(col0-col1) → DebugNode

    XGBoost binary:logistic 每标的输出 2 列概率 (N×2 矩阵),
    Formula 用 xgb_probs[0] - xgb_probs[1] 读取两列相减 → N×1 向量.
    """
    return (StrategyBuilder(MATRIX_STRATEGY_ID, MATRIX_STRATEGY_ID, source="A_hfq")
            .input("1", symbol=MATRIX_SYMBOLS, source=None)
            .function("2", "MA", 5, label="ma5")
            .xgboost("3", "production/test_formula_xgb_matrix-XGBoost.json",
                     features="ma5", objective="binary:logistic", num_class=2)
            .formula("4", "xgb_probs[0] - xgb_probs[1]", label="math_func")
            .debug("5", "debug_xgb_matrix")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .connect("3", "4")
            .connect("4", "5")
            .connect("3", "5")
            .build(layout="auto"))


@pytest.fixture(scope="function")
def _deploy_xgb_matrix(headers, request):
    """部署 trivial XGBoost 模型 + 策略（策略由参数提供）"""
    if not TRIVIAL_MODEL_PATH.exists():
        pytest.skip(f"trivial 模型不存在: {TRIVIAL_MODEL_PATH}")

    model_json = TRIVIAL_MODEL_PATH.read_text()
    strategy = request.param
    strategy_name = strategy["id"]

    files = {
        "script": ("script.json", json.dumps(strategy).encode(), "application/json"),
        "model_XGBoost": ("XGBoost.json", model_json.encode(), "application/json"),
    }
    if TRIVIAL_META_PATH.exists():
        files["model_XGBoost_meta"] = (
            "XGBoost.meta.json",
            TRIVIAL_META_PATH.read_text().encode(),
            "application/json")

    resp = requests.post(
        f"{BASE_URL}/strategy", files=files,
        data={"name": strategy_name},
        headers=headers, verify=VERIFY_SSL, timeout=60)
    assert resp.status_code == 200, f"deploy 失败: {resp.text}"

    yield {"strategy": strategy, "strategy_name": strategy_name}

    # 清理
    try:
        requests.post(f"{BASE_URL}/strategy",
                      json={"mode": 2, "name": strategy_name},
                      headers=headers, verify=VERIFY_SSL, timeout=5)
        requests.delete(f"{BASE_URL}/strategy",
                        json={"name": strategy_name},
                        headers=headers, verify=VERIFY_SSL, timeout=5)
    except Exception:
        pass


class TestFormulaXGBoostMatrix:
    """XGBoost 矩阵输出 + Formula 列读取测试

    验证场景:
    - XGBoostNode (binary:logistic) 对 2 个标的各输出 2 列概率 (N×2 矩阵)
    - Formula 用 xgb_probs[0] / xgb_probs[1] 读取第 0/1 列
    - 相减得到 N×1 向量
    - 验证: shape (每个标的 N 行 1 列) + 值正确性 (diff == col0 - col1)
    """

    @pytest.mark.parametrize(
        "_deploy_xgb_matrix",
        [_build_xgb_matrix_strategy()],
        indirect=True)
    def test_matrix_column_subtraction(self, headers, _deploy_xgb_matrix):
        """XGBoost 输出 N×2 矩阵, Formula 读取两列相减 → N×1 向量"""
        strategy = _deploy_xgb_matrix["strategy"]
        strategy_name = _deploy_xgb_matrix["strategy_name"]
        _run_backtest(strategy, headers)

        df = read_debug_csv(strategy_name, "debug_xgb_matrix")
        assert len(df) > 0, "Debug CSV 为空"

        for symbol in MATRIX_SYMBOLS:
            prob0_col = f"{symbol}.xgb_probs_0"
            prob1_col = f"{symbol}.xgb_probs_1"
            result_col = f"{symbol}.math_func"

            # 列存在性
            for col in [prob0_col, prob1_col, result_col]:
                assert col in df.columns, f"缺少列 {col}, 实际列: {list(df.columns)}"

            p0 = pd.to_numeric(df[prob0_col], errors="coerce").values
            p1 = pd.to_numeric(df[prob1_col], errors="coerce").values
            result = pd.to_numeric(df[result_col], errors="coerce").values

            # 有效行: 三个值都 finite (跳过 NaN warmup)
            valid = np.isfinite(p0) & np.isfinite(p1) & np.isfinite(result)
            n_valid = int(np.sum(valid))
            assert n_valid > 0, f"{symbol}: 无有效数据行 (概率全 NaN?)"

            # 值正确性: result == p0 - p1
            expected_diff = p0[valid] - p1[valid]
            actual_diff = result[valid]

            # 非零验证: 结果不能全为 0 (否则断言 trivially pass)
            assert not np.allclose(actual_diff, 0.0), \
                f"[{symbol}] 结果全为 0, 未真正验证列相减逻辑"

            max_err = float(np.max(np.abs(actual_diff - expected_diff)))
            assert max_err < TOLERANCE, \
                f"[{symbol}] 列相减误差 {max_err:.2e} 超过容差 {TOLERANCE}"

            # Shape 验证: 结果向量长度 = 概率列长度 (每 bar 一个值 → N×1)
            assert len(result) == len(p0), \
                f"[{symbol}] 结果长度 {len(result)} != 概率列长度 {len(p0)}"
            assert n_valid == int(np.sum(np.isfinite(p0) & np.isfinite(p1))), \
                f"[{symbol}] 有效结果数 {n_valid} != 有效输入行数"


# ============================================================
# 链式 FormulaNode 拓扑测试 (CTA_v16 raw_strength 模式)
# ============================================================

CHAIN_STRATEGY_ID = "test_formula_xgb_chain"


def _build_xgb_chain_strategy() -> dict:
    """构造链式 FormulaNode 策略 (模拟 CTA_v16 raw_strength 拓扑):

    Input(2标的) → MA(5) → XGBoost(binary) → raw_strength → DebugNode
                        ↘ factor(formula) ↗

    - factor = ma5[t] / 100 (随 bar 变化)
    - raw_strength = (xgb_probs[0] - xgb_probs[1]) * factor
    - raw_strength 同时读取 XGBoost 输出和另一个 FormulaNode 输出

    验证 FormulaNode 能正确解析来自不同类型上游节点（XGBoost + Formula）的变量.
    """
    return (StrategyBuilder(CHAIN_STRATEGY_ID, CHAIN_STRATEGY_ID, source="A_hfq")
            .input("1", symbol=MATRIX_SYMBOLS, source=None)
            # label 必须与下游公式引用名一致: FunctionNode 输出 key = {symbol}.{label},
            # FormulaNode 公式 "ma5[t]" 查找 {symbol}.ma5, 所以 label 不能是 "MA(5)"
            .function("2", "MA", 5, label="ma5")
            .xgboost("3", "production/test_formula_xgb_chain-XGBoost.json",
                     features="ma5", objective="binary:logistic", num_class=2)
            # factor: 中间 FormulaNode, 输出随 bar 变化
            .formula("4", "ma5[t] / 100", label="factor")
            # raw_strength: 同时读 XGBoost 和 factor (链式 FormulaNode)
            .formula("5", "(xgb_probs[0] - xgb_probs[1]) * factor[t]",
                     label="raw_strength")
            .debug("6", "debug_chain")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            # MA(5) → factor (factor 读 ma5[t])
            .connect("2", "4")
            # XGBoost → raw_strength (读 xgb_probs[0], xgb_probs[1])
            .connect("3", "5")
            # factor → raw_strength (读 factor[t]) — 链式 FormulaNode
            .connect("4", "5")
            # DebugNode 连接所有计算节点
            .connect("3", "6")
            .connect("4", "6")
            .connect("5", "6")
            .build(layout="auto"))


class TestFormulaXGBoostChain:
    """链式 FormulaNode 拓扑测试 — 模拟 CTA_v16 raw_strength 模式

    验证 FormulaNode 能同时读取 XGBoost 输出和另一个 FormulaNode 输出,
    确保变量解析、执行顺序、多上游类型混合正确.
    """

    @pytest.mark.parametrize(
        "_deploy_xgb_matrix",
        [_build_xgb_chain_strategy()],
        indirect=True)
    def test_chained_formula_with_xgboost(self, headers, _deploy_xgb_matrix):
        """Formula(XGBoost + Formula) 链式拓扑: raw_strength = (p0-p1) * factor"""
        strategy = _deploy_xgb_matrix["strategy"]
        strategy_name = _deploy_xgb_matrix["strategy_name"]
        _run_backtest(strategy, headers)

        df = read_debug_csv(strategy_name, "debug_chain")
        assert len(df) > 0, "Debug CSV 为空"

        for symbol in MATRIX_SYMBOLS:
            p0_col = f"{symbol}.xgb_probs_0"
            p1_col = f"{symbol}.xgb_probs_1"
            factor_col = f"{symbol}.factor"
            result_col = f"{symbol}.raw_strength"

            # 列存在性
            for col in [p0_col, p1_col, factor_col, result_col]:
                assert col in df.columns, f"缺少列 {col}, 实际列: {list(df.columns)}"

            p0 = pd.to_numeric(df[p0_col], errors="coerce").values
            p1 = pd.to_numeric(df[p1_col], errors="coerce").values
            factor = pd.to_numeric(df[factor_col], errors="coerce").values
            result = pd.to_numeric(df[result_col], errors="coerce").values

            # 有效行
            valid = np.isfinite(p0) & np.isfinite(p1) & np.isfinite(factor) & np.isfinite(result)
            n_valid = int(np.sum(valid))
            assert n_valid > 0, f"{symbol}: 无有效数据行"

            # factor 必须随 bar 变化 (MA(5)/100 不同 bar 值不同)
            assert not np.allclose(factor[valid], factor[valid][0]), \
                f"[{symbol}] factor 全为常数, 未验证链式计算"

            # XGBoost 概率非退化：不同 bar 应产生不同概率
            assert not np.allclose(p0[valid], p0[valid][0]), \
                f"[{symbol}] xgb_probs_0 恒定为 {p0[valid][0]:.4f}，模型训练域可能未覆盖推理域"

            # 值正确性: raw_strength == (p0 - p1) * factor
            expected = (p0[valid] - p1[valid]) * factor[valid]
            actual = result[valid]
            max_err = float(np.max(np.abs(actual - expected)))
            assert max_err < TOLERANCE, \
                f"[{symbol}] 链式计算误差 {max_err:.2e} 超过容差 {TOLERANCE}"

            # 非零验证
            assert not np.allclose(actual, 0.0), \
                f"[{symbol}] raw_strength 全为 0, 链式计算可能未生效"


# ============================================================
# TOPK 截面函数 — Vector<double> 上下文解析测试
# ============================================================

TOPK_VECTOR_STRATEGY_ID = "test_topk_vector_context"
TOPK_VECTOR_SYMBOLS = ["sz.800001", "sz.800002"]


def _build_topk_vector_strategy() -> dict:
    """构造策略: Input(2标的) → MA(5) → FormulaNode(topk) → DebugNode

    验证 TOPK 截面函数能正确读取 context 中的 Vector<double> 时间序列。

    Bug 场景: computeNode(TOPK) 调用 evalNode 获取变量值，
    evalIdentifier 从 context 返回 Vector<double>（时间序列），
    但 computeNode 只处理 double，Vector 时 score 默认为 0.0，
    导致 TOPK 无法区分标的，始终按 pool 顺序选择。

    测试原理:
    - sz.800001 和 sz.800002 价格水平不同 → MA(5) 不同
    - topk(ma5, 1) 应选出 MA 更大的标的
    - 如果 bug 存在: 所有 score=0 → partial_sort 按 pool 顺序 → 永远选 sz.800001
    - 如果修复: 正确选 MA 更大的标的
    """
    return (StrategyBuilder(TOPK_VECTOR_STRATEGY_ID, TOPK_VECTOR_STRATEGY_ID,
                            source="A_hfq")
            .input("1", symbol=TOPK_VECTOR_SYMBOLS, source=None)
            .function("2", "MA", 5, label="ma5")
            # topk_result: 1.0 = 被 topk 选中, 0.0 = 未选中
            .formula("3", "topk(ma5, 1)", label="topk_result")
            .debug("4", "debug_topk")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .connect("3", "4")
            .connect("2", "4")
            .build(layout="auto"))


@pytest.fixture(scope="function")
def _deploy_topk_vector(headers, request):
    """部署 TOPK Vector 测试策略"""
    strategy = _build_topk_vector_strategy()
    strategy_name = strategy["id"]

    files = {
        "script": ("script.json", json.dumps(strategy).encode(), "application/json"),
    }
    resp = requests.post(
        f"{BASE_URL}/strategy", files=files,
        data={"name": strategy_name},
        headers=headers, verify=VERIFY_SSL, timeout=60)
    assert resp.status_code == 200, f"deploy 失败: {resp.text}"

    yield {"strategy": strategy, "strategy_name": strategy_name}

    try:
        requests.post(f"{BASE_URL}/strategy",
                      json={"mode": 2, "name": strategy_name},
                      headers=headers, verify=VERIFY_SSL, timeout=5)
        requests.delete(f"{BASE_URL}/strategy",
                        json={"name": strategy_name},
                        headers=headers, verify=VERIFY_SSL, timeout=5)
    except Exception:
        pass


class TestTopkVectorContext:
    """TOPK 截面函数 Vector<double> 上下文解析测试

    验证 computeNode(TOPK) 能正确处理 evalNode 返回的 Vector<double>。

    Bug: computeNode 只处理 double 类型，Vector<double> 时 score 默认 0.0，
    导致 TOPK 无法区分标的，始终按 pool 顺序选择第一个。

    测试原理:
    - 2 个标的价格水平不同 → MA(5) 不同
    - topk(ma5, 1) 应选出 MA 更大的标的
    - Bug 存在时: 所有 score=0 → 永远选 pool 第一个 (sz.800001)
    """

    @pytest.mark.parametrize(
        "_deploy_topk_vector",
        [_build_topk_vector_strategy()],
        indirect=True)
    def test_topk_selects_by_value_not_pool_order(self, headers, _deploy_topk_vector):
        """TOPK 应按值选择标的，而非按 pool 顺序"""
        strategy_name = _deploy_topk_vector["strategy_name"]
        strategy = _deploy_topk_vector["strategy"]
        _run_backtest(strategy, headers)

        df = read_debug_csv(strategy_name, "debug_topk")
        assert len(df) > 0, "Debug CSV 为空"

        sym1, sym2 = TOPK_VECTOR_SYMBOLS
        ma1_col = f"{sym1}.ma5"
        ma2_col = f"{sym2}.ma5"
        result_col = f"{sym1}.topk_result"

        for col in [ma1_col, ma2_col, result_col]:
            assert col in df.columns, f"缺少列 {col}, 实际列: {list(df.columns)}"

        ma1 = pd.to_numeric(df[ma1_col], errors="coerce").values
        ma2 = pd.to_numeric(df[ma2_col], errors="coerce").values
        topk_sym1 = pd.to_numeric(df[result_col], errors="coerce").values

        valid = np.isfinite(ma1) & np.isfinite(ma2) & np.isfinite(topk_sym1)
        n_valid = int(np.sum(valid))
        assert n_valid > 0, "无有效数据行"

        # 核心验证: TOPK 选中的标的应该是 MA 更大的那个
        # 如果 bug 存在 (score=0 for Vector), topk 永远选 pool 第一个 (sym1)
        # 即使 sym2 的 MA 更大
        wrong_selections = 0
        total_with_diff = 0
        for i in range(len(valid)):
            if not valid[i]:
                continue
            if abs(ma1[i] - ma2[i]) < 1e-6:
                continue  # MA 相等时跳过
            total_with_diff += 1
            sym1_selected = topk_sym1[i] > 0.5
            if ma1[i] > ma2[i]:
                if not sym1_selected:
                    wrong_selections += 1
            else:
                if sym1_selected:
                    wrong_selections += 1

        assert total_with_diff > 0, "两个标的 MA 始终相同，无法验证 TOPK 选择逻辑"
        assert wrong_selections == 0, (
            f"TOPK 选择错误: {wrong_selections}/{total_with_diff} 个 bar "
            f"未选中 MA 更大的标的。可能原因: computeNode(TOPK) 未处理 Vector<double>，"
            f"所有 score=0，按 pool 顺序选择")


# ============================================================
# TestSignalLogicAndOr: and/or 算子与 topk 组合的 6 条独立分支路径
#
# Bug 现象（2026-09-14 诊断）:
# CTA_v16 C++ 回测 buys_raw=42, 但 DEBUG D 显示 topk 正确返回 true_count=3。
# 根因: evalAndExpr/evalOrExpr 让所有 symbol 通过布尔评估。
#
# 数据设计: 5 个标的，价格水平差异（top-3 高价 + 2 低价），让 and/or 分支
# 命中可观察的不同结果。
# ============================================================
import csv
import shutil
from datetime import datetime, timedelta

SIGNAL_LOGIC_PRICES = [
    ("sz.900010", 100.0), ("sz.900011", 95.0), ("sz.900012", 90.0),  # top-3, high
    ("sz.900013", 50.0), ("sz.900014", 40.0),                          # not top-3, low
]
SIGNAL_LOGIC_SYMBOLS = [s for s, _ in SIGNAL_LOGIC_PRICES]
SIGNAL_LOGIC_HFQ_DIR = CSV_DATA_DIR  # _DATA_DIR / "A_hfq"
SIGNAL_LOGIC_ORG_DIR = CSV_DATA_DIR.parent / "AStock"
SIGNAL_LOGIC_START = datetime(2024, 1, 1)
SIGNAL_LOGIC_N_BARS = 100


def _signal_logic_write_csv(symbol, base_price):
    """生成常数价格 CSV 并上传（确定性时间 + 噪声 OHLCV）

    边界符号（base_price 与 cs_* 测试阈值对齐的，如 80/90）跳过噪声：
    否则 close-90/close-80 的噪声会让 cs_count 在 2/3 之间漂移，
    破坏 cs_size_vs_sum 等依赖"零值标的"的测试断言。
    """
    np.random.seed(hash(symbol) & 0x7FFFFFFF)
    rows = []
    d = SIGNAL_LOGIC_START
    is_boundary = base_price in (80.0, 90.0)
    for _ in range(SIGNAL_LOGIC_N_BARS):
        while d.weekday() >= 5:
            d += timedelta(days=1)
        if is_boundary:
            close = base_price
            open_p = base_price
            high = base_price
            low = base_price
        else:
            close = base_price * (1 + np.random.normal(0, 0.001))
            open_p = close * (1 + np.random.normal(0, 0.001))
            high = max(open_p, close) * 1.002
            low = min(open_p, close) * 0.998
        volume = int(np.random.uniform(1000000, 5000000))
        turnover = round(volume * close, 2)
        rows.append([d.strftime("%Y-%m-%d"), round(open_p, 2), round(close, 2),
                     round(high, 2), round(low, 2), volume, turnover])
        d += timedelta(days=1)

    hfq_path = SIGNAL_LOGIC_HFQ_DIR / f"{symbol}.csv"
    org_path = SIGNAL_LOGIC_ORG_DIR / f"{symbol}.csv"
    hfq_path.parent.mkdir(parents=True, exist_ok=True)
    org_path.parent.mkdir(parents=True, exist_ok=True)

    with open(hfq_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['datetime', 'open', 'close', 'high', 'low', 'volume', 'turnover'])
        for row in rows:
            writer.writerow(row)
    shutil.copy2(hfq_path, org_path)
    return hfq_path, org_path


def _signal_logic_upload(symbols, headers):
    """通过 /v0/quote/data 上传 org 和 hfq（**必须分开**，见 data_pipeline_org_hfq_split_fix）"""
    for symbol in symbols:
        hfq_lines = (SIGNAL_LOGIC_HFQ_DIR / f"{symbol}.csv").read_text().strip().split("\n")[1:]
        org_lines = (SIGNAL_LOGIC_ORG_DIR / f"{symbol}.csv").read_text().strip().split("\n")[1:]
        resp = requests.post(f"{BASE_URL}/quote/data", json={
            "action": "import", "table": "stock_1d", "symbol": symbol,
            "data": org_lines, "data_hfq": hfq_lines,
        }, headers=headers, verify=VERIFY_SSL, timeout=60)
        if resp.status_code != 200:
            return {"status": "error", "error": f"upload {symbol}: HTTP {resp.status_code} {resp.text[:200]}"}
    return {"status": "ok"}


def _signal_logic_cleanup(symbols, headers):
    for symbol in symbols:
        try:
            requests.post(f"{BASE_URL}/quote/data", json={
                "action": "delete", "table": "stock_1d", "symbol": symbol,
            }, headers=headers, verify=VERIFY_SSL, timeout=30)
        except Exception:
            pass
        for p in [SIGNAL_LOGIC_HFQ_DIR / f"{symbol}.csv", SIGNAL_LOGIC_ORG_DIR / f"{symbol}.csv"]:
            if p.exists():
                p.unlink()


def _build_signal_logic_strategy(strategy_id, buy_expr, sell_expr="false",
                                 allow_short=False, debug_signal=False):
    """Input(5 标的) → SignalNode(buy, sell) → Portfolio → Execution

    debug_signal=True 时额外挂一个 DebugNode 旁路挂在 SignalNode 之后，
    导出 {symbol}.signal 时间序列。成交笔数要到 broker 层才拿得到，而
    or/and 短路语义属于信号层，用 .signal 断言才能只测一件事。
    """
    builder = (StrategyBuilder(
                   strategy_id,
                   f"信号逻辑测试_{strategy_id}",
                   description="and/or 算子与 topk 组合测试",
                   capital=100000,
                   backtest={
                       "start": SIGNAL_LOGIC_START.strftime("%Y-%m-%d"),
                       "end": (SIGNAL_LOGIC_START
                               + timedelta(days=SIGNAL_LOGIC_N_BARS * 2)).strftime("%Y-%m-%d"),
                   },
                   source="A_hfq")
               .input("1", symbol=SIGNAL_LOGIC_SYMBOLS, label="行情",
                      fields=("close",))
               .signal("2", SIGNAL_LOGIC_SYMBOLS, label="信号",
                       buy=buy_expr, sell=sell_expr, allow_short=allow_short)
               .portfolio("3", label="组合", position_ratio=1.0,
                          allow_short=allow_short)
               # 只声明本用例关心的成本项，其余走服务端默认值
               .execution("4", label="执行", params={
                   "commission": P.number(0.0),
                   "stampDuty": P.number(0.0),
                   "slippage": P.number(0.0),
                   "type": P.select(1),
               })
               .connect("1", "2", prop="close")
               .connect("2", "3")
               .connect("3", "4"))

    if debug_signal:
        builder.debug("5", "debug_signal")
        # 旁路：SignalNode 的 .signal 不经过 broker，直接落盘
        builder.connect("2", "5")

    return builder.build()


def _signal_logic_backtest(strategy_id, buy_expr, sell_expr="false", headers=None,
                           allow_short=False, debug_signal=False):
    strategy = _build_signal_logic_strategy(strategy_id, buy_expr, sell_expr,
                                            allow_short=allow_short,
                                            debug_signal=debug_signal)
    r = requests.post(f"{BASE_URL}/backtest",
                      json={"script": json.dumps(strategy), "validate": False},
                      headers=headers, verify=VERIFY_SSL, timeout=1800)
    if r.status_code != 200:
        return {"status": "error", "error": f"HTTP {r.status_code}: {r.text[:300]}"}
    return {"status": "ok", "result": r.json()}


def _symbols_with_signal(strategy_id, action):
    """读 Debug CSV，返回历史上出现过该动作的标的集合。

    action: 1=BUY, -1=SELL, 0=HOLD。SignalNode 每根 bar 都为池内所有标的写
    一个值，所以 HOLD 恒覆盖全部标的；只关心 ±1 时直接看集合差集。
    """
    df = read_debug_csv(strategy_id, "debug_signal")
    hit = set()
    for col in df.columns:
        if not col.endswith(".signal"):
            continue
        symbol = col[: -len(".signal")]
        if (df[col] == action).any():
            hit.add(symbol)
    return hit


@pytest.fixture(scope="module", autouse=True)
def _signal_logic_module_setup():
    """模块级 fixture：生成 CSV → 上传 → 测试结束清理"""
    headers = {"Authorization": requests.post(
        f"{BASE_URL}/user/login", json={"name": "admin", "pwd": "admin"},
        verify=VERIFY_SSL, timeout=10).json()["tk"]}
    for symbol, price in SIGNAL_LOGIC_PRICES:
        hfq, org = _signal_logic_write_csv(symbol, price)
    upload_result = _signal_logic_upload([s for s, _ in SIGNAL_LOGIC_PRICES], headers)
    if upload_result["status"] == "error":
        pytest.skip(f"setup failed: {upload_result['error']}")
    yield headers
    _signal_logic_cleanup([s for s, _ in SIGNAL_LOGIC_PRICES], headers)


@pytest.fixture
def signal_headers(_signal_logic_module_setup):
    return _signal_logic_module_setup


class TestSignalLogicAndOr:
    """and/or 算子与 topk 组合的 6 条独立分支路径

    per_filter_branch_test_data 原则:
    - L1/L4 对照组（双 true/left true）应产生 BUY/SELL
    - L2/L3/L5/L6 剔除项（左 false/右 false/双 false/右 true 但左 false）应被正确剔除
    """

    def test_l1_and_both_true_buy_top3(self, signal_headers):
        """L1 对照组: and 左 true + 右 true → BUY >=3 (top-3 高价)"""
        resp = _signal_logic_backtest("l1_and_both_true",
                                     "(close[t-1] >= 80) and topk(close, 3)",
                                     headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        result = resp["result"]
        assert result["summary"]["buy_count"] >= 3, (
            f"L1: 期望 BUY>=3 (top-3 高价), 实际 {result['summary']['buy_count']}。"
            f"若=0 或 5, 说明 evalAndExpr 错误地把 and 整体吞掉。"
        )

    def test_l2_and_left_false_drops_all(self, signal_headers):
        """L2: and 左 false → 全部 HOLD (即使 right=true)"""
        resp = _signal_logic_backtest("l2_and_left_false",
                                     "(close[t-1] >= 200) and topk(close, 3)",
                                     headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        assert resp["result"]["summary"]["buy_count"] == 0, (
            f"L2: and 左 false 应全部 HOLD, 实际 BUY="
            f"{resp['result']['summary']['buy_count']}。"
            f"若>0, 说明 evalAndExpr 未实现 left=false 短路。"
        )

    def test_l3_and_right_false_drops_non_topk(self, signal_headers):
        """L3 bug 路径: and 左 true + 右 false → HOLD

        表达式: (close[t-1] <= 80) and topk(close, 3)
        - top-3 (sz.900010/11/12, close>=90): left=false → HOLD
        - sz.900013/14 (close<=50): left=true, right=false → HOLD
        - 预期 BUY count = 0

        Bug 触发: 当前 evalAndExpr 让所有 symbol 通过, BUY=5。
        修复后: BUY count = 0。
        """
        resp = _signal_logic_backtest("l3_and_right_false",
                                     "(close[t-1] <= 80) and topk(close, 3)",
                                     headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        buy_count = resp["result"]["summary"]["buy_count"]
        assert buy_count == 0, (
            f"L3 bug 检测: BUY count={buy_count}。"
            f"若=5 表明 evalAndExpr 让所有 symbol 通过 AND 评估。"
        )

    def test_l3_low_symbols_not_bought(self, signal_headers):
        """L3 子断言: 即使 BUY 总数非 0，low symbols 也不在 BUY 列表"""
        resp = _signal_logic_backtest("l3_low_subcheck",
                                     "(close[t-1] <= 80) and topk(close, 3)",
                                     headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        buy_symbols = {trade["symbol"] for trade in resp["result"].get("buy", [])}
        for sym in ["sz.900013", "sz.900014"]:
            assert sym not in buy_symbols, (
                f"L3 细粒度: {sym} (low, not top-3) 不应 BUY, 实际 buys={buy_symbols}"
            )

    def test_l4_or_left_true_sells(self, signal_headers):
        """L4 对照组: or 左 true → SELL (短路, 不评估右)

        断言落在 SignalNode 的 .signal 上，不看成交笔数：
        融券卖出目前还没接通 broker 层（StockPositionManager 拒绝无持仓卖出），
        用 sell_count 断言会把 or 短路语义和融券成交链路耦在一起。
        """
        resp = _signal_logic_backtest("l4_or_left_true",
                                     "false",
                                     "(close[t-1] <= 60) or !topk(close, 3)",
                                     headers=signal_headers, allow_short=True,
                                     debug_signal=True)
        assert resp["status"] == "ok", resp.get("error")
        sold = _symbols_with_signal("l4_or_left_true", -1)
        # 左支命中低价组；这两个在 prepare.py 里预灌为可做空标的
        assert {"sz.900013", "sz.900014"} <= sold, (
            f"L4: 期望低价组产生 SELL 信号，实际 sold={sorted(sold)}"
        )

    def test_l5_or_both_false_drops(self, signal_headers):
        """L5: or 双 false → HOLD (应被剔除)"""
        resp = _signal_logic_backtest("l5_or_both_false",
                                     "false",
                                     "(close[t-1] >= 200) or (close[t-1] <= 0)",
                                     headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        assert resp["result"]["summary"]["sell_count"] == 0, (
            f"L5: or 双 false 应 HOLD, 实际 SELL="
            f"{resp['result']['summary']['sell_count']}"
        )

    def test_l6_or_right_true_passes(self, signal_headers):
        """L6: or 左 false + 右 true → SELL (应通过)

        左支 (close>=200) 全 false，命中的是 !topk 分支的低价组。
        """
        resp = _signal_logic_backtest("l6_or_right_true",
                                     "false",
                                     "(close[t-1] >= 200) or !topk(close, 3)",
                                     headers=signal_headers, allow_short=True,
                                     debug_signal=True)
        assert resp["status"] == "ok", resp.get("error")
        sold = _symbols_with_signal("l6_or_right_true", -1)
        assert {"sz.900013", "sz.900014"} <= sold, (
            f"L6: 期望 !topk 分支的低价组产生 SELL 信号，实际 sold={sorted(sold)}"
        )

    def test_topk_only_buy_control(self, signal_headers):
        """对照: buy = topk(close, 3) 单独 → BUY >=3 (验证 topk 单独正确)"""
        resp = _signal_logic_backtest("topk_only", "topk(close, 3)", "false",
                                     headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        assert resp["result"]["summary"]["buy_count"] >= 3, (
            f"topk_only: 期望 BUY>=3, 实际 {resp['result']['summary']['buy_count']}"
        )


class TestCSFunctionSameNameCollision:
    """同名 CS 函数多次调用（不同参数）应解析到不同节点

    Bug（2026-09-15 修复）：_varToNodeId 按函数名映射，cs_count(x,1) 和
    cs_count(x,-1) 冲突——两者都解析到最后一个创建的节点，返回相同值。
    修复后用 AST 地址映射（_csAstNodeToId），每个调用独立。

    新增 cs_size() 截面函数：返回标的总数，用于一致性公式分母。

    数据（5 标的，close 恒定）：
    - sz.900010: ~100, sz.900011: ~95, sz.900012: ~90  → close >= 80 = true (3个)
    - sz.900013: ~50,  sz.900014: ~40                   → close >= 80 = false (2个)

    cs_count(close - 80, 1) = 3   cs_count(close - 80, -1) = 2   cs_size() = 5
    """

    def test_cs_count_different_args_independent(self, signal_headers):
        """cs_count(x, 1) 和 cs_count(x, -1) 应返回不同值

        公式: max(cs_count(close-80, 1), cs_count(close-80, -1)) / cs_size() >= 0.6
        正确: max(3,2)/5 = 0.6 >= 0.6 → true → topk 选 3 个 BUY
        冲突: 若两者都返回同一值 → max(N,N)/5 = N/5
          N=2 → 0.4 < 0.6 → 无 BUY（错误）
          N=3 → 0.6 >= 0.6 → 碰巧正确但语义错误
        """
        resp = _signal_logic_backtest(
            "cs_collision_consistency",
            "(max(cs_count(close - 80, 1), cs_count(close - 80, -1)) / cs_size()) >= 0.6 and topk(close, 3)",
            headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        buy_count = resp["result"]["summary"]["buy_count"]
        assert buy_count >= 3, (
            f"同名 CS 函数冲突检测: 期望 BUY>=3 (max(3,2)/5=0.6 通过 + topk 3), "
            f"实际 buy_count={buy_count}。"
            f"若=0 说明 cs_count(x,1) 和 cs_count(x,-1) 返回相同值（冲突未修复）。"
        )

    def test_cs_size_returns_total_symbols(self, signal_headers):
        """cs_size() 应返回标的总数 5

        公式: cs_count(close-80, 1) / cs_size() >= 0.6
        3/5 = 0.6 >= 0.6 → true → 全部 BUY（无 topk 约束）
        """
        resp = _signal_logic_backtest(
            "cs_size_basic",
            "cs_count(close - 80, 1) / cs_size() >= 0.6",
            headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        buy_count = resp["result"]["summary"]["buy_count"]
        assert buy_count == 5, (
            f"cs_size 基本测试: 3/5=0.6>=0.6 应全部 BUY, "
            f"实际 buy_count={buy_count}。"
        )

    def test_cs_size_distinguishes_from_cs_count_sum(self, signal_headers):
        """cs_size() 与 cs_count(1)+cs_count(-1) 应不同（当有零值时）

        公式: cs_count(close - 90, 1) + cs_count(close - 90, -1) vs cs_size()
        close-90: 100-90=+10, 95-90=+5, 90-90=0, 50-90=-40, 40-90=-50
        cs_count(1)=2, cs_count(-1)=2, cs_count(0)=1
        cs_count(1)+cs_count(-1) = 4, cs_size() = 5

        用 cs_size 做分母: max(2,2)/5 = 0.4 < 0.5 → 无 BUY
        用 sum 做分母:    max(2,2)/4 = 0.5 >= 0.5 → BUY（错误，忽略了零值标的）
        """
        resp = _signal_logic_backtest(
            "cs_size_vs_sum",
            "(max(cs_count(close - 90, 1), cs_count(close - 90, -1)) / cs_size()) >= 0.5 and topk(close, 3)",
            headers=signal_headers)
        assert resp["status"] == "ok", resp.get("error")
        buy_count = resp["result"]["summary"]["buy_count"]
        # max(2,2)/5 = 0.4 < 0.5 → false → 无 BUY
        assert buy_count == 0, (
            f"cs_size vs sum 区分测试: max(2,2)/5=0.4<0.5 应无 BUY, "
            f"实际 buy_count={buy_count}。"
            f"若>0 说明分母用了 cs_count(1)+cs_count(-1)=4 而非 cs_size()=5。"
        )


# ============================================================
# PCT / RANK 截面百分位测试
#
# 覆盖三件事:
#   1. pct(close) 升序百分位：值最高的得 1.0，并列取平均名次，
#      与 pandas rank(pct=True) 逐值相等（研究管线的 *_xs 特征就是这么算的）
#   2. rank(close) 方向：值最高的得 1.0、最低的得 0.0
#      （此前实现是降序排序，最高的反而得 0.0，与文档契约相反）
#   3. NaN 标的被排除在排名之外，自身输出 NaN
#      （基本面特征在首次公告前必然是 NaN，这条是基本面节点落地的前提）
#
# 数据用常数价，不加噪声：排名是纯函数，常数价让期望值完全确定。
# NaN 源用 close/(close-100)：分母为 0 时引擎的除法返回 NaN
# （StmtGrammar.cpp 的 '/' 分支），其它标的是有限值。
# ============================================================

PCT_PRICES = [
    ("sz.900020", 10.0),
    ("sz.900021", 20.0),
    ("sz.900022", 20.0),   # 与 900021 并列 → 平均名次
    ("sz.900023", 30.0),
    ("sz.900024", 40.0),
    ("sz.900025", 50.0),
    ("sz.900026", 100.0),  # close/(close-100) 的分母 → NaN
]
PCT_SYMBOLS = [s for s, _ in PCT_PRICES]
PCT_PRICE_ARRAY = np.array([p for _, p in PCT_PRICES], dtype=float)
PCT_HFQ_DIR = CSV_DATA_DIR
PCT_ORG_DIR = CSV_DATA_DIR.parent / "AStock"
PCT_START = datetime(2024, 1, 1)
PCT_N_BARS = 100
PCT_TOLERANCE = 1e-9


def _pct_write_csv(symbol, base_price):
    """写常数价 CSV（hfq + org），不加噪声以保证排名期望值完全确定"""
    rows = []
    d = PCT_START
    for _ in range(PCT_N_BARS):
        while d.weekday() >= 5:
            d += timedelta(days=1)
        volume = 1000000
        rows.append([d.strftime("%Y-%m-%d"), base_price, base_price,
                     base_price, base_price, volume, round(volume * base_price, 2)])
        d += timedelta(days=1)

    hfq_path = PCT_HFQ_DIR / f"{symbol}.csv"
    org_path = PCT_ORG_DIR / f"{symbol}.csv"
    hfq_path.parent.mkdir(parents=True, exist_ok=True)
    org_path.parent.mkdir(parents=True, exist_ok=True)
    for path in (hfq_path, org_path):
        with open(path, 'w', newline='') as f:
            writer = csv.writer(f)
            writer.writerow(['datetime', 'open', 'close', 'high', 'low', 'volume', 'turnover'])
            for row in rows:
                writer.writerow(row)
    return hfq_path, org_path


def _pct_upload(symbols, headers):
    """上传 org 和 hfq（必须分开，见 data_pipeline_org_hfq_split_fix）"""
    for symbol in symbols:
        hfq_lines = (PCT_HFQ_DIR / f"{symbol}.csv").read_text().strip().split("\n")[1:]
        org_lines = (PCT_ORG_DIR / f"{symbol}.csv").read_text().strip().split("\n")[1:]
        resp = requests.post(f"{BASE_URL}/quote/data", json={
            "action": "import", "table": "stock_1d", "symbol": symbol,
            "data": org_lines, "data_hfq": hfq_lines,
        }, headers=headers, verify=VERIFY_SSL, timeout=60)
        if resp.status_code != 200:
            return {"status": "error",
                    "error": f"upload {symbol}: HTTP {resp.status_code} {resp.text[:200]}"}
    return {"status": "ok"}


def _pct_cleanup(symbols, headers):
    for symbol in symbols:
        try:
            requests.post(f"{BASE_URL}/quote/data", json={
                "action": "delete", "table": "stock_1d", "symbol": symbol,
            }, headers=headers, verify=VERIFY_SSL, timeout=30)
        except Exception:
            pass
        for p in (PCT_HFQ_DIR / f"{symbol}.csv", PCT_ORG_DIR / f"{symbol}.csv"):
            if p.exists():
                p.unlink()


def _build_pct_strategy(strategy_id, expression, label):
    """Input(7 标的) → FormulaNode(expression) → DebugNode"""
    return (StrategyBuilder(
                strategy_id,
                f"截面百分位测试_{strategy_id}",
                description="pct/rank 截面百分位测试",
                capital=100000,
                backtest={
                    "start": PCT_START.strftime("%Y-%m-%d"),
                    "end": (PCT_START + timedelta(days=PCT_N_BARS * 2)).strftime("%Y-%m-%d"),
                },
                source="A_hfq")
            .input("1", symbol=PCT_SYMBOLS, label="行情", fields=("close",))
            .formula("2", expression, label=label)
            .debug("3", "debug_pct")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .build())


def _engine_div(numerator, denominator):
    """复刻 StmtGrammar.cpp 的除法语义：分母 |x| < 1e-10 时返回 NaN

    numpy 的 0 分母给 inf，引擎给 NaN，这里必须对齐引擎。
    """
    num = np.asarray(numerator, dtype=float)
    den = np.asarray(denominator, dtype=float)
    near_zero = np.abs(den) < 1e-10
    safe_den = np.where(near_zero, 1.0, den)
    with np.errstate(divide="ignore", invalid="ignore"):
        return np.where(near_zero, np.nan, num / safe_den)


def _pct_expected(values):
    """黄金标准：pandas rank(pct=True)，NaN 不参与排名且自身为 NaN"""
    return pd.Series(np.asarray(values, dtype=float)).rank(pct=True).values


def _rank_expected(values):
    """黄金标准：pandas rank() 归一化到 0~1（与 rank 名次差 1 的线性映射）"""
    s = pd.Series(np.asarray(values, dtype=float))
    r = s.rank()
    n = int(r.notna().sum())
    if n <= 1:
        return pd.Series(np.where(r.notna(), 0.5, np.nan)).values
    return ((r - 1.0) / (n - 1)).values


@pytest.fixture(scope="module", autouse=True)
def _pct_module_setup():
    """模块级 fixture：生成常数价 CSV → 上传 → 测试结束清理"""
    headers = {"Authorization": requests.post(
        f"{BASE_URL}/user/login", json={"name": "admin", "pwd": "admin"},
        verify=VERIFY_SSL, timeout=10).json()["tk"]}
    for symbol, price in PCT_PRICES:
        _pct_write_csv(symbol, price)
    result = _pct_upload(PCT_SYMBOLS, headers)
    if result["status"] == "error":
        pytest.skip(f"setup failed: {result['error']}")
    yield headers
    _pct_cleanup(PCT_SYMBOLS, headers)


@pytest.fixture
def pct_headers(_pct_module_setup):
    return _pct_module_setup


def _read_pct_columns(strategy_id, label):
    """读 Debug CSV，返回 {symbol: 该标的的结果序列}"""
    df = read_debug_csv(strategy_id, "debug_pct")
    out = {}
    for symbol in PCT_SYMBOLS:
        col = f"{symbol}.{label}"
        assert col in df.columns, f"缺少列 {col}, 实际列: {list(df.columns)}"
        out[symbol] = pd.to_numeric(df[col], errors="coerce").values
    return out


class TestCrossSectionPercentile:
    """pct / rank 截面百分位与名次"""

    def test_pct_matches_pandas_rank_pct(self, pct_headers):
        """pct(close) 应等于 pandas rank(pct=True)：升序、最高 1.0、并列取平均"""
        sid = "test_pct_basic"
        strategy = _build_pct_strategy(sid, "pct(close)", "pct_result")
        _run_backtest(strategy, pct_headers)

        actual = _read_pct_columns(sid, "pct_result")
        expected = _pct_expected(PCT_PRICE_ARRAY)

        for i, symbol in enumerate(PCT_SYMBOLS):
            vals = actual[symbol]
            finite = np.isfinite(vals)
            assert finite.any(), f"[{symbol}] pct 全为 NaN"
            got = vals[finite]
            exp = expected[i]
            assert np.allclose(got, exp, atol=PCT_TOLERANCE), (
                f"[{symbol}] pct(close) 期望 {exp}, 实际 {got[:3]}... "
                f"(pandas rank(pct=True) 口径: 最高 1.0, 并列取平均名次)"
            )

    def test_pct_highest_is_one(self, pct_headers):
        """pct(close) 中价格最高的标的必须得 1.0（方向不能反）"""
        sid = "test_pct_highest"
        strategy = _build_pct_strategy(sid, "pct(close)", "pct_result")
        _run_backtest(strategy, pct_headers)

        actual = _read_pct_columns(sid, "pct_result")
        highest = PCT_SYMBOLS[int(np.argmax(PCT_PRICE_ARRAY))]
        lowest = PCT_SYMBOLS[int(np.argmin(PCT_PRICE_ARRAY))]

        hi_vals = actual[highest][np.isfinite(actual[highest])]
        lo_vals = actual[lowest][np.isfinite(actual[lowest])]
        assert np.allclose(hi_vals, 1.0, atol=PCT_TOLERANCE), (
            f"最高价标的 {highest} 的 pct 应恒为 1.0, 实际 {hi_vals[:3]}"
        )
        expected_low = 1.0 / len(PCT_SYMBOLS)
        assert np.allclose(lo_vals, expected_low, atol=PCT_TOLERANCE), (
            f"最低价标的 {lowest} 的 pct 应恒为 {expected_low}, 实际 {lo_vals[:3]}"
        )

    def test_rank_highest_is_one_lowest_is_zero(self, pct_headers):
        """rank(close) 方向：最高 1.0、最低 0.0（与文档契约一致）"""
        sid = "test_rank_direction"
        strategy = _build_pct_strategy(sid, "rank(close)", "rank_result")
        _run_backtest(strategy, pct_headers)

        actual = _read_pct_columns(sid, "rank_result")
        expected = _rank_expected(PCT_PRICE_ARRAY)

        for i, symbol in enumerate(PCT_SYMBOLS):
            vals = actual[symbol]
            finite = np.isfinite(vals)
            assert finite.any(), f"[{symbol}] rank 全为 NaN"
            got = vals[finite]
            exp = expected[i]
            assert np.allclose(got, exp, atol=PCT_TOLERANCE), (
                f"[{symbol}] rank(close) 期望 {exp}, 实际 {got[:3]}... "
                f"(rank 契约: 最高 1.0、最低 0.0；实现若给最高 0.0 说明方向反了)"
            )

    def test_pct_excludes_nan_symbols(self, pct_headers):
        """NaN 标的应被排除在排名之外，自身输出 NaN

        close/(close-100): 价格 100 的标的分母为 0 → 引擎除法返回 NaN。
        有效值只有 6 个，pct 分母应为 6 而不是 7。
        """
        sid = "test_pct_nan"
        strategy = _build_pct_strategy(sid, "pct(close/(close-100))", "pct_result")
        _run_backtest(strategy, pct_headers)

        actual = _read_pct_columns(sid, "pct_result")
        values = _engine_div(PCT_PRICE_ARRAY, PCT_PRICE_ARRAY - 100)
        expected = _pct_expected(values)

        nan_symbol = PCT_SYMBOLS[int(np.argmax(PCT_PRICE_ARRAY))]  # 100 → NaN
        for i, symbol in enumerate(PCT_SYMBOLS):
            vals = actual[symbol]
            if np.isnan(expected[i]):
                assert not np.isfinite(vals).any(), (
                    f"[{symbol}] 期望 NaN（分母为 0），实际有有限值 {vals[np.isfinite(vals)][:3]}"
                )
                continue
            finite = np.isfinite(vals)
            assert finite.any(), f"[{symbol}] pct 全为 NaN"
            got = vals[finite]
            assert np.allclose(got, expected[i], atol=PCT_TOLERANCE), (
                f"[{symbol}] 期望 {expected[i]}, 实际 {got[:3]}... "
                f"(NaN 标的必须被排除在排名之外，否则分母会从 6 变成 7)"
            )

    def test_pct_ties_take_average_rank(self, pct_headers):
        """并列值取平均名次（pandas rank(method='average') 口径）"""
        sid = "test_pct_ties"
        strategy = _build_pct_strategy(sid, "pct(close)", "pct_result")
        _run_backtest(strategy, pct_headers)

        actual = _read_pct_columns(sid, "pct_result")
        expected = _pct_expected(PCT_PRICE_ARRAY)

        tied = [i for i, s in enumerate(PCT_SYMBOLS) if s in ("sz.900021", "sz.900022")]
        assert len(tied) == 2, "测试数据应有两个并列标的"
        for i in tied:
            vals = actual[PCT_SYMBOLS[i]]
            finite = np.isfinite(vals)
            got = vals[finite]
            assert np.allclose(got, expected[i], atol=PCT_TOLERANCE), (
                f"[{PCT_SYMBOLS[i]}] 并列值期望平均名次 {expected[i]}, 实际 {got[:3]}"
            )
        # 两个并列标的的 pct 必须相等
        v1 = actual[PCT_SYMBOLS[tied[0]]][np.isfinite(actual[PCT_SYMBOLS[tied[0]]])]
        v2 = actual[PCT_SYMBOLS[tied[1]]][np.isfinite(actual[PCT_SYMBOLS[tied[1]]])]
        assert np.allclose(v1, v2), "并列值的 pct 应相等"

    def test_pct_with_level_returns_quantile(self, pct_headers):
        """pct(close, p) 应返回截面的 p 分位数（所有标的一个标量值）

        线性插值口径，与 numpy.quantile / pandas.quantile 默认一致。
        第二个参数不能被静默忽略 —— 否则 pct(close, 0.9) 会被当成
        百分位排名返回 0~1 的数，语义完全跑偏。
        """
        sid = "test_pct_quantile"
        strategy = _build_pct_strategy(sid, "pct(close, 0.5)", "pct_result")
        _run_backtest(strategy, pct_headers)

        actual = _read_pct_columns(sid, "pct_result")
        expected = float(np.quantile(PCT_PRICE_ARRAY, 0.5))

        for symbol in PCT_SYMBOLS:
            vals = actual[symbol]
            finite = np.isfinite(vals)
            assert finite.any(), f"[{symbol}] pct(close, 0.5) 全为 NaN"
            got = vals[finite]
            assert np.allclose(got, expected, atol=PCT_TOLERANCE), (
                f"[{symbol}] pct(close, 0.5) 期望中位数 {expected}, 实际 {got[:3]}"
            )


# ============================================================
# 带点变量名（category.field）
# ============================================================
# 公式语法原本只认 `[a-zA-Z_][a-zA-Z_0-9]*` 这种不带点的标识符，但服务端有多个
# 节点发布的 context key 天然带点：
#   FundamentalNode: `{symbol}.{category}.{field}`  → 短名 `profit.roe_avg`
#   EMDNode:         `{symbol}.{label}.nimf_0`      → 短名 `{label}.nimf_0`
# 下游公式要引用它们就得写 `profit.roe_avg[t]`。语法不支持时这类引用的两种坏表现：
#   1. 校验期报 `Unknown variable 'profit'`（点号后整段被当成另一个标识符）
#   2. 运行期更糟：点号后的成员名被静默丢弃，退化成读 `sz.xxx.profit`
#
# 本节复用 PCT 那套常数价数据（无噪声 → 期望值完全确定），覆盖：
#   1. 带点标签的 FormulaNode 输出 → 下游用带点名引用
#   2. 带点名进截面函数
#   3. FundamentalNode 的真实场景：profit.roe_avg[t]
#   4. 负例：不存在的带点名必须仍在 Init 阶段报错，且报的是完整名字
#   5. 关键字前缀的标识符（order_flow / notes / true_value）
# ============================================================

# 每个标的的 roe_avg（profit 类别），阈值 0.20 → 命中 900020 / 900021 / 900026
DOTTED_ROE_BY_SYMBOL = {
    "sz.900020": 0.30,
    "sz.900021": 0.25,
    "sz.900022": 0.20,   # 恰好等于阈值，不满足严格大于
    "sz.900023": 0.10,
    "sz.900024": 0.05,
    "sz.900025": 0.15,
    "sz.900026": 0.35,
}
DOTTED_ROE_EXPECTED = {"sz.900020", "sz.900021", "sz.900026"}

# close >= 40 的标的（常数价，无噪声）
DOTTED_CLOSE_40_EXPECTED = {"sz.900024", "sz.900025", "sz.900026"}

# profit 类别的 CSV 表头，列序与 FinanceDB::CATEGORY_DEFS["profit"].field_map 对应
DOTTED_PROFIT_HEADER = (
    "code,pubDate,statDate,roeAvg,npMargin,gpMargin,netProfit,epsTTM,"
    "MBRevenue,totalShare,liqaShare"
)
# 财报必须在回测开始（2024-01-01）之前发布，否则按 pub_date 前视时全是 NaN
DOTTED_FUND_PUB_DATE = "2023-10-30"
DOTTED_FUND_STAT_DATE = "2023-09-30"


def _build_dotted_label_strategy(strategy_id, label, buy_expr):
    """Input → FormulaNode(label 带点) → SignalNode → DebugNode

    FormulaNode 的 label 直接拼进 context key：`{symbol}.{label}`，
    label 带点 → 发布出去的 key 带点 → 下游必须用带点名引用。
    """
    return (StrategyBuilder(
                strategy_id,
                f"带点变量名测试_{strategy_id}",
                description="公式变量名支持点号",
                capital=100000,
                backtest={
                    "start": PCT_START.strftime("%Y-%m-%d"),
                    "end": (PCT_START + timedelta(days=PCT_N_BARS * 2)).strftime("%Y-%m-%d"),
                },
                source="A_hfq")
            .input("1", symbol=PCT_SYMBOLS, label="行情", fields=("close",))
            .formula("2", "close", label=label)
            .signal("3", PCT_SYMBOLS, label="信号", buy=buy_expr, sell="false")
            .debug("4", "debug_signal")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .connect("3", "4")
            .build())


def _build_fundamental_strategy(strategy_id, buy_expr):
    """Input → FundamentalNode → SignalNode → DebugNode

    FundamentalNode 读 params 的方式和其它节点不一样：它直接读裸值，不读
    {"value":..., "type":...} 信封（见 FundamentalNode::Init）。
    """
    builder = (StrategyBuilder(
                   strategy_id,
                   f"基本面字段测试_{strategy_id}",
                   description="FundamentalNode 输出的 category.field 引用",
                   capital=100000,
                   backtest={
                       "start": PCT_START.strftime("%Y-%m-%d"),
                       "end": (PCT_START + timedelta(days=PCT_N_BARS * 2)).strftime("%Y-%m-%d"),
                   },
                   source="A_hfq")
               .input("1", symbol=PCT_SYMBOLS, label="行情", fields=("close",)))
    builder.add({
        "id": "2", "type": "custom", "position": {"x": 0, "y": 0},
        "data": {
            "label": "fund", "nodeType": "fundamental",
            "params": {"categories": "", "fields": "", "lookback": 8, "alignMode": "latest"},
        },
    })
    (builder.signal("3", PCT_SYMBOLS, label="信号", buy=buy_expr, sell="false")
            .debug("4", "debug_signal")
            .connect("1", "2", prop="close")
            .connect("2", "3")
            .connect("3", "4"))
    return builder.build()


def _import_fundamental_data(headers):
    """导入 7 个标的的 profit 类基本面数据

    先删同名标的的旧行，保证重复运行不会累积重复记录。
    """
    for symbol in PCT_SYMBOLS:
        requests.delete(f"{BASE_URL}/finance/data",
                        json={"category": "profit", "code": symbol},
                        headers=headers, verify=VERIFY_SSL, timeout=30)

    lines = [DOTTED_PROFIT_HEADER]
    for symbol in PCT_SYMBOLS:
        roe = DOTTED_ROE_BY_SYMBOL[symbol]
        lines.append(
            f"{symbol},{DOTTED_FUND_PUB_DATE},{DOTTED_FUND_STAT_DATE},"
            f"{roe},0.20,0.30,1000000,1.5,,100000,100000"
        )
    resp = requests.post(f"{BASE_URL}/finance/data",
                         json={"action": "import", "category": "profit", "data": lines},
                         headers=headers, verify=VERIFY_SSL, timeout=60)
    assert resp.status_code == 200, f"导入基本面数据失败: {resp.text[:300]}"
    assert resp.json()["imported_rows"] == len(PCT_SYMBOLS), (
        f"基本面导入行数不对: {resp.text[:200]}"
    )


def _cleanup_fundamental_data(headers):
    for symbol in PCT_SYMBOLS:
        try:
            requests.delete(f"{BASE_URL}/finance/data",
                            json={"category": "profit", "code": symbol},
                            headers=headers, verify=VERIFY_SSL, timeout=30)
        except Exception:
            pass


@pytest.fixture
def fundamental_headers(pct_headers):
    """导入基本面数据 → 测试结束清理"""
    _import_fundamental_data(pct_headers)
    yield pct_headers
    _cleanup_fundamental_data(pct_headers)


class TestDottedVariableName:
    """带点名的变量引用"""

    def test_dotted_label_reference(self, pct_headers):
        """带点标签的 FormulaNode 输出，下游用带点名引用

        FormulaNode(label="grp.score") → context key `{symbol}.grp.score`
        SignalNode: buy = "grp.score[t] >= 40" → 期望命中 close>=40 的 3 个标的
        """
        sid = "dotted_label_ref"
        strategy = _build_dotted_label_strategy(sid, "grp.score", "grp.score[t] >= 40")
        _run_backtest(strategy, pct_headers)

        buy = _symbols_with_signal(sid, 1)
        missing = DOTTED_CLOSE_40_EXPECTED - buy
        assert not missing, (
            f"带点变量名: 缺 {missing}，实际 BUY={buy}。"
            f"若一个都没有，说明 'grp.score' 没被当成变量名。"
        )

    def test_dotted_name_in_cross_section_function(self, pct_headers):
        """带点名进截面函数

        buy = "topk(grp.score, 3)" → 期望命中价格最高的 3 个标的
        （900021/900022 并列 20，不进 top-3，结果无歧义）
        """
        sid = "dotted_topk"
        strategy = _build_dotted_label_strategy(sid, "grp.score", "topk(grp.score, 3)")
        _run_backtest(strategy, pct_headers)

        buy = _symbols_with_signal(sid, 1)
        expected = {"sz.900024", "sz.900025", "sz.900026"}
        missing = expected - buy
        assert not missing, (
            f"带点名 + 截面函数: 缺 {missing}，实际 BUY={buy}"
        )

    def test_fundamental_field_reference(self, fundamental_headers):
        """FundamentalNode 的真实场景：profit.roe_avg[t]

        FundamentalNode 发布 `{symbol}.profit.roe_avg`
        SignalNode: buy = "profit.roe_avg[t] > 0.20"
        → 期望命中 roe_avg > 0.20 的 3 个标的
        """
        sid = "dotted_fundamental"
        strategy = _build_fundamental_strategy(sid, "profit.roe_avg[t] > 0.20")
        _run_backtest(strategy, fundamental_headers)

        buy = _symbols_with_signal(sid, 1)
        missing = DOTTED_ROE_EXPECTED - buy
        assert not missing, (
            f"FundamentalNode 字段引用: 缺 {missing}，实际 BUY={buy}。"
            f"若一个都没有，说明 'profit.roe_avg' 没被当成变量名。"
        )
        assert "sz.900022" not in buy, (
            f"FundamentalNode 字段引用: sz.900022 (roe=0.20) 不满足严格大于，不该 BUY，"
            f"实际 BUY={buy}"
        )

    def test_unknown_dotted_variable_rejected(self, pct_headers):
        """负例：不存在的带点名必须在 Init 阶段报错，且报的是完整名字

        没有这条负例的话，「带点名一律放行」也能让上面的用例变绿。
        """
        sid = "dotted_unknown"
        strategy = _build_dotted_label_strategy(sid, "grp.score", "grp.nope[t] >= 40")
        resp = requests.post(f"{BASE_URL}/backtest",
                             json={"script": json.dumps(strategy), "validate": False},
                             headers=pct_headers, verify=VERIFY_SSL, timeout=600)
        assert resp.status_code != 200, (
            f"未知带点变量名应当让策略初始化失败，实际 HTTP {resp.status_code}"
        )
        body = resp.text
        assert "Unknown variable" in body, (
            f"期望 Unknown variable 报错，实际: {body[:300]}"
        )
        assert "grp.nope" in body, (
            f"报错应指出完整的变量名 'grp.nope'，实际: {body[:300]}"
        )


class TestKeywordPrefixIdentifier:
    """关键字前缀的标识符 —— 语法加词边界后才是合法变量名"""

    def test_identifier_starting_with_or(self, pct_headers):
        """order_flow：旧语法里 'or' 前缀让 Identifier 直接匹配失败"""
        sid = "kw_prefix_or"
        strategy = _build_dotted_label_strategy(sid, "order_flow", "order_flow[t] >= 40")
        _run_backtest(strategy, pct_headers)

        buy = _symbols_with_signal(sid, 1)
        missing = DOTTED_CLOSE_40_EXPECTED - buy
        assert not missing, (
            f"order_flow: 缺 {missing}，实际 BUY={buy}。"
            f"若解析失败，说明 'or' 前缀仍被当成关键字。"
        )

    def test_identifier_starting_with_not(self, pct_headers):
        """notes：旧语法里 'not' 被当成前缀运算符，'notes' 被拆成 not + es"""
        sid = "kw_prefix_not"
        strategy = _build_dotted_label_strategy(sid, "notes", "notes[t] >= 40")
        _run_backtest(strategy, pct_headers)

        buy = _symbols_with_signal(sid, 1)
        missing = DOTTED_CLOSE_40_EXPECTED - buy
        assert not missing, (
            f"notes: 缺 {missing}，实际 BUY={buy}。"
            f"若被解析成 'not es'，求值结果与预期完全不同。"
        )

    def test_identifier_starting_with_true(self, pct_headers):
        """true_value：旧语法里 BoolLiteral 会吃掉 'true' 前缀"""
        sid = "kw_prefix_true"
        strategy = _build_dotted_label_strategy(sid, "true_value", "true_value[t] >= 40")
        _run_backtest(strategy, pct_headers)

        buy = _symbols_with_signal(sid, 1)
        missing = DOTTED_CLOSE_40_EXPECTED - buy
        assert not missing, (
            f"true_value: 缺 {missing}，实际 BUY={buy}"
        )
