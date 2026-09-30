#!/usr/bin/env python3
"""
策略稳定性测试（14 维度）

所有稳定性测试集中在此文件，按 Tier 分组：
  Tier 0 — 数值复现性（本文件 TestReproducibility）
  Tier 1 — 交易成本敏感性 / 训练窗口敏感性 / Walk-forward
  Tier 2 — 市场状态条件稳定性 / 集中度分析 / 信号衰减
  Tier 3 — 参数扫描 / 输入扰动 / 压力测试
  Tier 4 — VMD 诊断 / 统计检验 / 递归预测 / 集成

使用方法：
  pytest test_stability.py -v                          # 全部
  pytest test_stability.py::TestReproducibility -v     # 只跑 Tier 0
"""

import json
import math
import requests
import pytest
from pathlib import Path
from tool import run_backtest, reclaim_capital

N_RUNS = 10
FLOAT_TOL = 1e-6

STRATEGY_PATH = Path(__file__).parent.parent / "script" / "ml.json"


def _load_strategy() -> str:
    with open(STRATEGY_PATH) as f:
        return json.dumps(json.load(f))


def _run_bt(headers: dict, auth_token: str) -> dict:
    reclaim_capital(auth_token, strategy="test_ml_repro")
    result = run_backtest(_load_strategy(), headers, validate=False)
    assert result.get("status") != "error", f"Backtest failed: {result.get('error')}"
    return result


def _compare_float(a: float, b: float, path: str) -> str | None:
    if math.isnan(a) and math.isnan(b):
        return None
    if abs(a - b) > FLOAT_TOL:
        return f"{path}: {a} vs {b} (diff={abs(a-b):.2e})"
    return None


def _compare_values(a, b, path: str) -> list[str]:
    """递归比较两个 JSON 值，返回差异列表"""
    diffs = []
    if isinstance(a, dict) and isinstance(b, dict):
        all_keys = set(a.keys()) | set(b.keys())
        for k in sorted(all_keys):
            if k not in a:
                diffs.append(f"{path}.{k}: missing in run A")
            elif k not in b:
                diffs.append(f"{path}.{k}: missing in run B")
            else:
                diffs.extend(_compare_values(a[k], b[k], f"{path}.{k}"))
    elif isinstance(a, list) and isinstance(b, list):
        if len(a) != len(b):
            diffs.append(f"{path}: length {len(a)} vs {len(b)}")
        for i in range(min(len(a), len(b))):
            diffs.extend(_compare_values(a[i], b[i], f"{path}[{i}]"))
    elif isinstance(a, float) and isinstance(b, float):
        err = _compare_float(a, b, path)
        if err:
            diffs.append(err)
    elif isinstance(a, (int, bool, str)) and isinstance(b, (int, bool, str)):
        if a != b:
            diffs.append(f"{path}: {a!r} vs {b!r}")
    else:
        if a != b:
            diffs.append(f"{path}: {a!r} vs {b!r} (type {type(a).__name__} vs {type(b).__name__})")
    return diffs


# 需要从比较中排除的字段（含随机性）
EXCLUDE_FIELDS = {"mc_paths"}


def _strip_excluded(result: dict) -> dict:
    """移除含随机性的字段"""
    return {k: v for k, v in result.items() if k not in EXCLUDE_FIELDS}


class TestReproducibility:
    """Tier 0: 数值复现性 — 同一策略 N 次运行结果必须一致"""

    def test_summary_reproducible(self, headers, auth_token):
        """summary 指标在 N 次运行中完全一致"""
        results = [_run_bt(headers, auth_token) for _ in range(N_RUNS)]
        baseline = results[0]["summary"]

        all_diffs = []
        for i, r in enumerate(results[1:], start=2):
            diffs = _compare_values(baseline, r["summary"], f"summary(run1 vs run{i})")
            all_diffs.extend(diffs)

        assert not all_diffs, (
            f"summary 不一致 ({len(all_diffs)} 处差异):\n"
            + "\n".join(all_diffs[:20])
        )

    def test_trades_reproducible(self, headers, auth_token):
        """buy/sell 交易记录在 N 次运行中完全一致"""
        results = [_run_bt(headers, auth_token) for _ in range(N_RUNS)]
        baseline_buy = results[0].get("buy", [])
        baseline_sell = results[0].get("sell", [])

        all_diffs = []
        for i, r in enumerate(results[1:], start=2):
            diffs = _compare_values(baseline_buy, r.get("buy", []), f"buy(run1 vs run{i})")
            diffs.extend(_compare_values(baseline_sell, r.get("sell", []), f"sell(run1 vs run{i})"))
            all_diffs.extend(diffs)

        assert not all_diffs, (
            f"交易记录不一致 ({len(all_diffs)} 处差异):\n"
            + "\n".join(all_diffs[:20])
        )

    def test_daily_returns_reproducible(self, headers, auth_token):
        """daily_returns 序列在 N 次运行中逐元素一致"""
        results = [_run_bt(headers, auth_token) for _ in range(N_RUNS)]
        baseline_dr = results[0].get("daily_returns", [])
        baseline_dd = results[0].get("daily_dates", [])

        all_diffs = []
        for i, r in enumerate(results[1:], start=2):
            diffs = _compare_values(baseline_dr, r.get("daily_returns", []), f"daily_returns(run1 vs run{i})")
            diffs.extend(_compare_values(baseline_dd, r.get("daily_dates", []), f"daily_dates(run1 vs run{i})"))
            all_diffs.extend(diffs)

        assert not all_diffs, (
            f"daily_returns 不一致 ({len(all_diffs)} 处差异):\n"
            + "\n".join(all_diffs[:20])
        )

    def test_features_reproducible(self, headers, auth_token):
        """features 指标在 N 次运行中完全一致"""
        results = [_run_bt(headers, auth_token) for _ in range(N_RUNS)]
        baseline = results[0].get("features", {})

        all_diffs = []
        for i, r in enumerate(results[1:], start=2):
            diffs = _compare_values(baseline, r.get("features", {}), f"features(run1 vs run{i})")
            all_diffs.extend(diffs)

        assert not all_diffs, (
            f"features 不一致 ({len(all_diffs)} 处差异):\n"
            + "\n".join(all_diffs[:20])
        )

    def test_protection_events_reproducible(self, headers, auth_token):
        """protection_events 在 N 次运行中完全一致（如有）"""
        results = [_run_bt(headers, auth_token) for _ in range(N_RUNS)]
        baseline = results[0].get("protection_events", [])

        # 如果没有 protection_events，跳过
        if not baseline:
            pytest.skip("策略无 protection_events")

        all_diffs = []
        for i, r in enumerate(results[1:], start=2):
            diffs = _compare_values(
                baseline, r.get("protection_events", []),
                f"protection_events(run1 vs run{i})"
            )
            all_diffs.extend(diffs)

        assert not all_diffs, (
            f"protection_events 不一致 ({len(all_diffs)} 处差异):\n"
            + "\n".join(all_diffs[:20])
        )

    def test_full_response_reproducible(self, headers, auth_token):
        """完整响应（排除 mc_paths）在 N 次运行中完全一致 — 兜底检查"""
        results = [_run_bt(headers, auth_token) for _ in range(N_RUNS)]
        baseline = _strip_excluded(results[0])

        all_diffs = []
        for i, r in enumerate(results[1:], start=2):
            candidate = _strip_excluded(r)
            diffs = _compare_values(baseline, candidate, f"full_response(run1 vs run{i})")
            all_diffs.extend(diffs)

        assert not all_diffs, (
            f"完整响应不一致 ({len(all_diffs)} 处差异，前 20 条):\n"
            + "\n".join(all_diffs[:20])
        )
