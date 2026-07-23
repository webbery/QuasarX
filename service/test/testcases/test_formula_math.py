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

urllib3.disable_warnings()

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

TEST_DIR = Path(__file__).parent / "node_test_data"
SERVICE_ROOT = Path(__file__).parent.parent.parent
DEBUG_DIR = SERVICE_ROOT / "build" / "data" / "data" / "debug"
CSV_DATA_DIR = SERVICE_ROOT / "build" / "data" / "A_hfq"

SYMBOL = "sz.800001"
TOLERANCE = 1e-6


# ============================================================
# Fixtures
# ============================================================

@pytest.fixture(scope="session")
def auth_token():
    r = requests.post(f"{BASE_URL}/user/login",
                      json={"name": "admin", "pwd": "admin"},
                      verify=VERIFY_SSL)
    return r.json().get("tk", "")


@pytest.fixture
def headers(auth_token):
    return {"Authorization": auth_token} if auth_token else {}


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


def _run_backtest(strategy: dict, headers: dict) -> dict:
    r = requests.post(f"{BASE_URL}/backtest",
                      json={"script": json.dumps(strategy), "validate": False},
                      headers=headers, verify=VERIFY_SSL)
    assert r.status_code == 200, f"Backtest failed: {r.text}"
    return r.json()


def _read_debug_csv(strategy_id: str, label: str) -> pd.DataFrame:
    csv_path = DEBUG_DIR / strategy_id / f"{label}.csv"
    assert csv_path.exists(), f"Debug CSV not found: {csv_path}"
    with open(csv_path) as f:
        first_line = f.readline().strip()
    columns = [col.split(":")[0] for col in first_line.split(",")]
    df = pd.read_csv(csv_path, skiprows=1, header=None, names=columns)
    return df


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

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.abs(ma5)
        _compare_series(actual, expected, label="abs")

    def test_exp(self, headers):
        """exp(ma5[t] / 100) — 指数（缩小避免溢出）"""
        sid = "test_math_exp"
        strategy = _build_strategy("exp(ma5[t] / 100)", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.exp(ma5 / 100)
        _compare_series(actual, expected, label="exp")

    def test_log(self, headers):
        """log(ma5[t]) — 自然对数"""
        sid = "test_math_log"
        strategy = _build_strategy("log(ma5[t])", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.log(ma5)
        _compare_series(actual, expected, label="log")

    def test_sqrt(self, headers):
        """sqrt(ma5[t]) — 平方根"""
        sid = "test_math_sqrt"
        strategy = _build_strategy("sqrt(ma5[t])", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.sqrt(ma5)
        _compare_series(actual, expected, label="sqrt")

    def test_sigmoid(self, headers):
        """sigmoid(ma5[t] - 100) — sigmoid 函数"""
        sid = "test_math_sigmoid"
        strategy = _build_strategy("sigmoid(ma5[t] - 100)", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = 1.0 / (1.0 + np.exp(-(ma5 - 100)))
        _compare_series(actual, expected, label="sigmoid")

    def test_min(self, headers):
        """min(ma5[t], 105) — 二元取小值"""
        sid = "test_math_min"
        strategy = _build_strategy("min(ma5[t], 105)", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.minimum(ma5, 105.0)
        _compare_series(actual, expected, label="min")

    def test_max(self, headers):
        """max(ma5[t], 95) — 二元取大值"""
        sid = "test_math_max"
        strategy = _build_strategy("max(ma5[t], 95)", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.maximum(ma5, 95.0)
        _compare_series(actual, expected, label="max")

    def test_min_two_variables(self, headers):
        """min(ma5[t], ma15[t]) — 两个变量取小值"""
        sid = "test_math_min2v"
        strategy = _build_two_input_strategy("min(ma5[t], ma15[t])", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
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

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        ma15 = _get_ma15_series().values
        expected = np.maximum(ma5, ma15)
        _compare_series(actual, expected, label="max_2vars")


class TestFormulaMathComposite:
    """复合公式测试 — 模拟 trend_strength 计算"""

    def test_clamp_abs_pattern(self, headers):
        """min(abs(x) * 50, 5) — 绝对值 + 缩放 + clamp"""
        sid = "test_comp_clamp_abs"
        strategy = _build_strategy("min(abs(ma5[t] - 100) * 50, 5)", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.minimum(np.abs(ma5 - 100) * 50, 5.0)
        _compare_series(actual, expected, label="clamp_abs")

    def test_trend_strength_formula(self, headers):
        """sigmoid((min(abs(a)*50, 5) + min(abs(b)*20, 5)) / 2)
        完整模拟 trend_strength 公式"""
        sid = "test_comp_trend_str"
        expr = ("sigmoid((min(abs(ma5[t] - 100) * 50, 5) "
                "+ min(abs(ma15[t] - 100) * 20, 5)) / 2)")
        strategy = _build_two_input_strategy(expr, sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
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

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.sqrt(np.abs(ma5 - 100))
        _compare_series(actual, expected, label="nested_sqrt_abs")

    def test_exp_neg_square(self, headers):
        """exp(-abs(ma5[t] - 100) / 10) — 高斯核风格"""
        sid = "test_comp_gauss"
        strategy = _build_strategy("exp(-abs(ma5[t] - 100) / 10)", sid)
        _run_backtest(strategy, headers)

        df = _read_debug_csv(sid, "debug_math")
        actual = pd.to_numeric(df[f"{SYMBOL}.math_func"], errors="coerce")

        ma5 = _get_ma5_series().values
        expected = np.exp(-np.abs(ma5 - 100) / 10)
        _compare_series(actual, expected, label="gaussian_kernel")
