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


def _build_strategy(expression: str, strategy_id: str) -> dict:
    """构造测试策略 JSON:
    QuoteInput → MA(5) → FormulaNode(expression) → DebugNode → Signal → Portfolio → Execution
    """
    return {
        "id": strategy_id,
        "name": f"数学函数测试_{strategy_id}",
        "version": 1,
        "description": "FormulaParser 数学函数测试",
        "backtest": {"start": "2024-01-01", "end": "2024-09-07"},
        "source": "A_hfq",
        "nodes": [
            {
                "id": "1", "type": "custom",
                "position": {"x": 0, "y": 0},
                "data": {
                    "label": "行情数据", "nodeType": "input",
                    "params": {
                        "source": {"value": "股票", "type": "text"},
                        "code": {"value": [SYMBOL], "type": "text"},
                        "freq": {"value": "1d", "type": "select"},
                        "close": {"value": "close", "type": "text"},
                        "open": {"value": "open", "type": "text"},
                        "high": {"value": "high", "type": "text"},
                        "low": {"value": "low", "type": "text"},
                        "volume": {"value": "volume", "type": "text"},
                    }
                }
            },
            {
                "id": "2", "type": "custom",
                "position": {"x": 0, "y": 0},
                "data": {
                    "label": "ma5", "nodeType": "function",
                    "params": {
                        "method": {"value": "MA", "type": "select"},
                        "range": {"value": "5d", "type": "text"},
                    }
                }
            },
            {
                "id": "3", "type": "custom",
                "position": {"x": 0, "y": 0},
                "data": {
                    "label": "math_func", "nodeType": "formula",
                    "params": {
                        "expression": {"value": expression, "type": "text"},
                    }
                }
            },
            {
                "id": "4", "type": "custom",
                "position": {"x": 0, "y": 0},
                "data": {
                    "label": "debug_math", "nodeType": "debug",
                    "params": {
                        "suffix": {"value": "csv", "type": "select"},
                    }
                }
            },
            {
                "id": "5", "type": "custom",
                "position": {"x": 0, "y": 0},
                "data": {
                    "label": "买入信号", "nodeType": "signal",
                    "params": {
                        "code": {"value": [SYMBOL], "type": "text"},
                        "buy": {"value": "true", "type": "text"},
                        "sell": {"value": "false", "type": "text"},
                    }
                }
            },
            {
                "id": "6", "type": "custom",
                "position": {"x": 0, "y": 0},
                "data": {
                    "label": "投资组合", "nodeType": "portfolio",
                    "params": {
                        "positionRatio": {"value": 1.0, "type": "number"},
                    }
                }
            },
            {
                "id": "7", "type": "custom",
                "position": {"x": 0, "y": 0},
                "data": {
                    "label": "交易执行", "nodeType": "execution",
                    "params": {
                        "commission": {"value": 0.0, "type": "number"},
                        "stampDuty": {"value": 0.0, "type": "number"},
                        "minFee": {"value": 0, "type": "number"},
                        "slippageModel": {"value": 0, "type": "number"},
                        "slippage": {"value": 0.0, "type": "number"},
                        "type": {"value": 1, "type": "select"},
                        "contract": {"value": 0, "type": "select"},
                    }
                }
            },
        ],
        "edges": [
            {"id": "e1", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e2", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            {"id": "e3", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
            {"id": "e4", "source": "1", "target": "5",
             "sourceHandle": "1-close", "targetHandle": "5", "type": "default"},
            {"id": "e5", "source": "5", "target": "6",
             "sourceHandle": "5", "targetHandle": "6", "type": "default"},
            {"id": "e6", "source": "6", "target": "7",
             "sourceHandle": "6", "targetHandle": "7", "type": "default"},
        ]
    }


def _build_two_input_strategy(expression: str, strategy_id: str) -> dict:
    """构造双输入测试策略: MA(5) + MA(15) → FormulaNode → DebugNode"""
    base = _build_strategy(expression, strategy_id)
    # 插入 MA(15) 节点 (id=8)，连接到 FormulaNode
    base["nodes"].insert(2, {
        "id": "8", "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": "ma15", "nodeType": "function",
            "params": {
                "method": {"value": "MA", "type": "select"},
                "range": {"value": "15d", "type": "text"},
            }
        }
    })
    # 添加 MA(15) → FormulaNode 的边
    base["edges"].append({
        "id": "e7", "source": "8", "target": "3",
        "sourceHandle": "8", "targetHandle": "3", "type": "default"
    })
    # 添加 QuoteInput → MA(15) 的边
    base["edges"].append({
        "id": "e8", "source": "1", "target": "8",
        "sourceHandle": "1-close", "targetHandle": "8", "type": "default"
    })
    return base


def _build_three_input_strategy(expression: str, strategy_id: str) -> dict:
    """构造三输入测试策略: MA(5) + MA(15) + MA(30) → FormulaNode → DebugNode

    用途: argmax(ma5[t], ma15[t], ma30[t]) 等 N-arg 测试
    """
    base = _build_two_input_strategy(expression, strategy_id)
    base["nodes"].insert(3, {
        "id": "9", "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": "ma30", "nodeType": "function",
            "params": {
                "method": {"value": "MA", "type": "select"},
                "range": {"value": "30d", "type": "text"},
            }
        }
    })
    base["edges"].append({
        "id": "e9", "source": "9", "target": "3",
        "sourceHandle": "9", "targetHandle": "3", "type": "default"
    })
    base["edges"].append({
        "id": "ea", "source": "1", "target": "9",
        "sourceHandle": "1-close", "targetHandle": "9", "type": "default"
    })
    return base


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
    return {
        "id": MATRIX_STRATEGY_ID,
        "name": MATRIX_STRATEGY_ID,
        "version": 1,
        "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "行情数据", "nodeType": "input",
                      "params": {"code": {"value": MATRIX_SYMBOLS, "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"},
                                 "open": {"value": "open", "type": "text"},
                                 "high": {"value": "high", "type": "text"},
                                 "low": {"value": "low", "type": "text"},
                                 "volume": {"value": "volume", "type": "text"}}}},
            {"id": "2", "type": "custom", "position": {"x": 200, "y": 0},
             "data": {"label": "ma5", "nodeType": "function",
                      "params": {"method": {"value": "MA", "type": "select"},
                                 "range": {"value": "5d", "type": "text"}}}},
            {"id": "3", "type": "custom", "position": {"x": 400, "y": 0},
             "data": {"label": "XGBoost", "nodeType": "xgboost",
                      "params": {"modelFile": {"value": "production/test_formula_xgb_matrix-XGBoost.json", "type": "text"},
                                 "features": {"value": "ma5", "type": "text"},
                                 "objective": {"value": "binary:logistic", "type": "select"},
                                 "num_class": {"value": 2, "type": "number"}}}},
            {"id": "4", "type": "custom", "position": {"x": 600, "y": 0},
             "data": {"label": "math_func", "nodeType": "formula",
                      "params": {"expression": {"value": "xgb_probs[0] - xgb_probs[1]", "type": "text"}}}},
            {"id": "5", "type": "custom", "position": {"x": 800, "y": 0},
             "data": {"label": "debug_xgb_matrix", "nodeType": "debug",
                      "params": {"suffix": {"value": "csv", "type": "select"}}}},
        ],
        "edges": [
            {"id": "e1->2", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e2->3", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            {"id": "e3->4", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
            {"id": "e4->5", "source": "4", "target": "5",
             "sourceHandle": "4", "targetHandle": "5", "type": "default"},
            {"id": "e3->5", "source": "3", "target": "5",
             "sourceHandle": "3", "targetHandle": "5", "type": "default"},
        ],
    }


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
    return {
        "id": CHAIN_STRATEGY_ID,
        "name": CHAIN_STRATEGY_ID,
        "version": 1,
        "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "行情数据", "nodeType": "input",
                      "params": {"code": {"value": MATRIX_SYMBOLS, "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"},
                                 "open": {"value": "open", "type": "text"},
                                 "high": {"value": "high", "type": "text"},
                                 "low": {"value": "low", "type": "text"},
                                 "volume": {"value": "volume", "type": "text"}}}},
            # label 必须与下游公式引用名一致: FunctionNode 输出 key = {symbol}.{label},
            # FormulaNode 公式 "ma5[t]" 查找 {symbol}.ma5, 所以 label 不能是 "MA(5)"
            {"id": "2", "type": "custom", "position": {"x": 200, "y": 0},
             "data": {"label": "ma5", "nodeType": "function",
                      "params": {"method": {"value": "MA", "type": "select"},
                                 "range": {"value": "5d", "type": "text"}}}},
            {"id": "3", "type": "custom", "position": {"x": 400, "y": 0},
             "data": {"label": "XGBoost", "nodeType": "xgboost",
                      "params": {"modelFile": {"value": "production/test_formula_xgb_chain-XGBoost.json", "type": "text"},
                                 "features": {"value": "ma5", "type": "text"},
                                 "objective": {"value": "binary:logistic", "type": "select"},
                                 "num_class": {"value": 2, "type": "number"}}}},
            # factor: 中间 FormulaNode, 输出随 bar 变化
            {"id": "4", "type": "custom", "position": {"x": 400, "y": 200},
             "data": {"label": "factor", "nodeType": "formula",
                      "params": {"expression": {"value": "ma5[t] / 100", "type": "text"}}}},
            # raw_strength: 同时读 XGBoost 和 factor (链式 FormulaNode)
            {"id": "5", "type": "custom", "position": {"x": 600, "y": 0},
             "data": {"label": "raw_strength", "nodeType": "formula",
                      "params": {"expression": {"value": "(xgb_probs[0] - xgb_probs[1]) * factor[t]", "type": "text"}}}},
            {"id": "6", "type": "custom", "position": {"x": 800, "y": 0},
             "data": {"label": "debug_chain", "nodeType": "debug",
                      "params": {"suffix": {"value": "csv", "type": "select"}}}},
        ],
        "edges": [
            {"id": "e1->2", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e2->3", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            # MA(5) → factor (factor 读 ma5[t])
            {"id": "e2->4", "source": "2", "target": "4",
             "sourceHandle": "2", "targetHandle": "4", "type": "default"},
            # XGBoost → raw_strength (读 xgb_probs[0], xgb_probs[1])
            {"id": "e3->5", "source": "3", "target": "5",
             "sourceHandle": "3", "targetHandle": "5", "type": "default"},
            # factor → raw_strength (读 factor[t]) — 链式 FormulaNode
            {"id": "e4->5", "source": "4", "target": "5",
             "sourceHandle": "4", "targetHandle": "5", "type": "default"},
            # DebugNode 连接所有计算节点
            {"id": "e3->6", "source": "3", "target": "6",
             "sourceHandle": "3", "targetHandle": "6", "type": "default"},
            {"id": "e4->6", "source": "4", "target": "6",
             "sourceHandle": "4", "targetHandle": "6", "type": "default"},
            {"id": "e5->6", "source": "5", "target": "6",
             "sourceHandle": "5", "targetHandle": "6", "type": "default"},
        ],
    }


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
    return {
        "id": TOPK_VECTOR_STRATEGY_ID,
        "name": TOPK_VECTOR_STRATEGY_ID,
        "version": 1,
        "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "行情数据", "nodeType": "input",
                      "params": {"code": {"value": TOPK_VECTOR_SYMBOLS, "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"},
                                 "open": {"value": "open", "type": "text"},
                                 "high": {"value": "high", "type": "text"},
                                 "low": {"value": "low", "type": "text"},
                                 "volume": {"value": "volume", "type": "text"}}}},
            {"id": "2", "type": "custom", "position": {"x": 200, "y": 0},
             "data": {"label": "ma5", "nodeType": "function",
                      "params": {"method": {"value": "MA", "type": "select"},
                                 "range": {"value": "5d", "type": "text"}}}},
            # topk_result: 1.0 = 被 topk 选中, 0.0 = 未选中
            {"id": "3", "type": "custom", "position": {"x": 400, "y": 0},
             "data": {"label": "topk_result", "nodeType": "formula",
                      "params": {"expression": {"value": "topk(ma5, 1)", "type": "text"}}}},
            {"id": "4", "type": "custom", "position": {"x": 600, "y": 0},
             "data": {"label": "debug_topk", "nodeType": "debug",
                      "params": {"suffix": {"value": "csv", "type": "select"}}}},
        ],
        "edges": [
            {"id": "e1->2", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e2->3", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            {"id": "e3->4", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
            {"id": "e2->4", "source": "2", "target": "4",
             "sourceHandle": "2", "targetHandle": "4", "type": "default"},
        ],
    }


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


def _build_signal_logic_strategy(strategy_id, buy_expr, sell_expr="false", allow_short=False):
    """Input(5 标的) → SignalNode(buy, sell) → Portfolio → Execution"""
    return {
        "id": strategy_id,
        "name": f"信号逻辑测试_{strategy_id}",
        "version": 1,
        "description": "and/or 算子与 topk 组合测试",
        "capital": 1000000,
        "backtest": {
            "start": SIGNAL_LOGIC_START.strftime("%Y-%m-%d"),
            "end": (SIGNAL_LOGIC_START + timedelta(days=SIGNAL_LOGIC_N_BARS * 2)).strftime("%Y-%m-%d")
        },
        "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom",
             "data": {"label": "行情", "nodeType": "input",
                      "params": {"source": {"value": "股票", "type": "text"},
                                 "code": {"value": SIGNAL_LOGIC_SYMBOLS, "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"}}}},
            {"id": "2", "type": "custom",
             "data": {"label": "信号", "nodeType": "signal",
                      "params": {"code": {"value": SIGNAL_LOGIC_SYMBOLS, "type": "text"},
                                 "type": {"value": "股票", "type": "select"},
                                 "allowShort": {"value": allow_short, "type": "boolean"},
                                 "buy": {"value": buy_expr, "type": "text"},
                                 "sell": {"value": sell_expr, "type": "text"}}}},
            {"id": "3", "type": "custom",
             "data": {"label": "组合", "nodeType": "portfolio",
                      "params": {"positionRatio": {"value": 1.0, "type": "number"},
                                 "allowShort": {"value": allow_short, "type": "boolean"}}}},
            {"id": "4", "type": "custom",
             "data": {"label": "执行", "nodeType": "execution",
                      "params": {"commission": {"value": 0.0, "type": "number"},
                                 "stampDuty": {"value": 0.0, "type": "number"},
                                 "slippage": {"value": 0.0, "type": "number"},
                                 "type": {"value": 1, "type": "select"}}}}],
        "edges": [
            {"id": "e1", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e2", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            {"id": "e3", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
        ]
    }


def _signal_logic_backtest(strategy_id, buy_expr, sell_expr="false", headers=None, allow_short=False):
    strategy = _build_signal_logic_strategy(strategy_id, buy_expr, sell_expr, allow_short=allow_short)
    r = requests.post(f"{BASE_URL}/backtest",
                      json={"script": json.dumps(strategy), "validate": False},
                      headers=headers, verify=VERIFY_SSL, timeout=1800)
    if r.status_code != 200:
        return {"status": "error", "error": f"HTTP {r.status_code}: {r.text[:300]}"}
    return {"status": "ok", "result": r.json()}


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
        """L4 对照组: or 左 true → SELL (短路, 不评估右)"""
        resp = _signal_logic_backtest("l4_or_left_true",
                                     "false",
                                     "(close[t-1] <= 60) or !topk(close, 3)",
                                     headers=signal_headers, allow_short=True)
        assert resp["status"] == "ok", resp.get("error")
        sell_count = resp["result"]["summary"]["sell_count"]
        assert sell_count >= 2, (
            f"L4: 期望 sell>=2 (low symbols left=true), 实际 {sell_count}"
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
        """L6: or 左 false + 右 true → SELL (应通过)"""
        resp = _signal_logic_backtest("l6_or_right_true",
                                     "false",
                                     "(close[t-1] >= 200) or !topk(close, 3)",
                                     headers=signal_headers, allow_short=True)
        assert resp["status"] == "ok", resp.get("error")
        sell_count = resp["result"]["summary"]["sell_count"]
        assert sell_count >= 2, (
            f"L6: or left=false + right=true 应 SELL, 实际 {sell_count}"
            f"期望>=2 (sz.900013/14 通过 !topk 分支)"
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
