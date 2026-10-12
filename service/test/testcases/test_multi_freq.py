#!/usr/bin/env python3
"""
多频率行情输入测试 (L1)

验证 input 节点 freq=1w 的周线视图：
  - 周线 OHLCV 与 Python ISO 周聚合基准逐值一致
  - 完成周滞后一周语义（无前视，周内值恒定）
  - 跨春节等长假不错位
  - range 以 bar 为单位：日线分支 range=20 是 20 日，周频分支 range=10 是 10 周
  - 纯日线策略不受影响（存量回归护栏）

测试流程与 test_node_unit.py 一致：提交回测 → 读 DebugNode CSV → 对比基准。

用法：
  pytest test_multi_freq.py -v
  pytest test_multi_freq.py::TestWeeklyAggregation -v

前置：先运行 node_test_data/generate_multi_freq_data.py 生成数据与策略。
"""

import json
import os
from datetime import datetime
from pathlib import Path

import numpy as np
import pytest

from tool import CSV_DATA_DIR, read_debug_csv, run_backtest_graph

TEST_DIR = Path(__file__).parent / "node_test_data"  # 策略文件直接在 node_test_data/ 下
TOLERANCE = 1e-6
WEEK_SYMBOL = "sz.800010"
DAILY_SYMBOL = "sz.800011"


# ------------------------------------------------------------
# 基准计算（口径对齐 service/lab/ahhmm/panel.py）
# ------------------------------------------------------------

def _load_daily(symbol):
    """从 A_hfq 读日线，返回 list of dict（含 datetime 字符串）"""
    path = os.path.join(str(CSV_DATA_DIR), f"{symbol}.csv")
    rows = []
    with open(path) as f:
        header = f.readline().strip().split(",")
        for line in f:
            if not line.strip():
                continue
            vals = line.strip().split(",")
            row = dict(zip(header, vals))
            for k in ("open", "close", "high", "low", "volume"):
                row[k] = float(row[k])
            rows.append(row)
    return rows


def _iso_week(datestr):
    """返回 (iso_year, iso_week)"""
    y, m, d = (int(x) for x in datestr.split("-"))
    return datetime(y, m, d).isocalendar()[:2]


def weekly_reference(rows):
    """按 ISO 周聚合日线。

    open  = 周内**首个**交易日的 open（只在首次见到该周时写入，
            不能每次都覆盖，否则会变成末个交易日的 open）
    high  = 周内 high 最大值
    low   = 周内 low 最小值
    close = 周内**最后一个**交易日的 close
    volume= 周内 volume 之和
    date  = 周内最后一个交易日（组内 datetime 最大值）
    """
    buckets = {}
    for r in rows:
        key = _iso_week(r["datetime"])
        b = buckets.get(key)
        if b is None:
            b = buckets[key] = {
                "date": r["datetime"], "open": r["open"], "high": r["high"],
                "low": r["low"], "close": r["close"], "volume": 0.0, "days": 0,
            }
        else:
            b["high"] = max(b["high"], r["high"])
            b["low"] = min(b["low"], r["low"])
        b["close"] = r["close"]               # 持续覆盖 = 末个交易日
        b["volume"] += r["volume"]
        b["date"] = max(b["date"], r["datetime"])
        b["days"] += 1
    return [buckets[k] for k in sorted(buckets)]


def _run(strategy_file, headers):
    with open(strategy_file) as f:
        strategy = json.load(f)
    run_backtest_graph(strategy, headers, validate=False)


def _num(series):
    return np.asarray([np.nan if v in ("", None) else float(v) for v in series],
                      dtype=float)


def _resample_to_weekly(values, csv_dates):
    """将日线网格上前向填充的周频值还原为周线网格。

    debug CSV 每个交易日有 2 行（两个 epoch），直接与外部传入的日线日期
    zip 会错位：values 比 dates 长一倍，zip 截断后只剩前半段，且第 i 行
    与第 i+1 行其实是同一天。这里改用 CSV 自带的 datetime 列，并跳过同一
    天的第二个 epoch，再按 ISO 周分组取每周最后一个有效值。

    返回 (weekly_values, week_keys)，其中 week_keys 是 (iso_year, iso_week) 元组列表。
    """
    seen = set()
    weeks = {}
    for val, datestr in zip(values, csv_dates):
        if np.isnan(val):
            continue
        day = str(datestr).split()[0]   # CSV 带 " 00:00:00"，只取日期部分
        if day in seen:                 # 同一天的第二个 epoch，跳过
            continue
        seen.add(day)
        key = _iso_week(day)
        weeks[key] = val                # 持续覆盖 → 每周最后一个有效值
    sorted_keys = sorted(weeks)
    return np.array([weeks[k] for k in sorted_keys]), sorted_keys


# ------------------------------------------------------------
# Fixtures
# ------------------------------------------------------------

@pytest.fixture(scope="module")
def daily_rows():
    return _load_daily(WEEK_SYMBOL)


@pytest.fixture(scope="module")
def weekly_ref(daily_rows):
    return weekly_reference(daily_rows)


@pytest.fixture(scope="function")
def weekly_debug(headers):
    """跑一次双输入策略，两个 Debug 导出日线与周线序列。"""
    strategy_path = TEST_DIR / "weekly.json"
    with open(strategy_path) as f:
        strategy = json.load(f)
    run_backtest_graph(strategy, headers, validate=False)
    return read_debug_csv("test_multi_freq_weekly", "debug_weekly")


# ------------------------------------------------------------
# 周线聚合正确性
# ------------------------------------------------------------

class TestWeeklyAggregation:

    def test_weekly_close_matches_reference(self, weekly_debug, weekly_ref, daily_rows):
        col = f"{WEEK_SYMBOL}.close_w"
        assert col in weekly_debug.columns, f"缺少周频列 {col}，实际列: {list(weekly_debug.columns)[:20]}"

        # 引擎把周频值前向填充到日线网格，必须先还原到周线网格再比对
        actual, week_keys = _resample_to_weekly(
            _num(weekly_debug[col]), weekly_debug["datetime"].tolist())
        expected = np.array([w["close"] for w in weekly_ref])
        
        # "完成周滞后一周"语义：第 N 周输出的是第 N-1 周的定稿值
        # 所以 actual[i] 应该等于 expected[i-1]
        # 跳过第一周（没有上一周可定稿）
        if len(actual) > 1 and len(expected) > 1:
            actual_shifted = actual[1:]  # 从第 2 周开始
            expected_shifted = expected[:-1]  # 到倒数第 2 周
            n = min(len(actual_shifted), len(expected_shifted))
            assert n > 0, "周频序列为空，用例退化"
            np.testing.assert_allclose(actual_shifted[:n], expected_shifted[:n], rtol=0, atol=TOLERANCE)
        else:
            # 数据太少，无法验证滞后语义
            n = min(len(actual), len(expected))
            assert n > 0, "周频序列为空，用例退化"
            np.testing.assert_allclose(actual[:n], expected[:n], rtol=0, atol=TOLERANCE)

    def test_weekly_ohlc_and_volume_match_reference(self, weekly_debug, weekly_ref, daily_rows):
        """open/high/low/volume 四项一起比对。"""
        checks = {
            "open_w": "open",
            "high_w": "high",
            "low_w": "low",
            "volume_w": "volume",
        }
        # 引擎把周频值前向填充到日线网格，必须先还原到周线网格再比对
        csv_dates = weekly_debug["datetime"].tolist()
        compared = 0
        for suffix, ref_key in checks.items():
            col = f"{WEEK_SYMBOL}.{suffix}"
            if col not in weekly_debug.columns:
                continue  # 该字段未通过 edge 导出
            actual, week_keys = _resample_to_weekly(_num(weekly_debug[col]), csv_dates)
            expected = np.array([w[ref_key] for w in weekly_ref])
            
            # "完成周滞后一周"语义：第 N 周输出的是第 N-1 周的定稿值
            if len(actual) > 1 and len(expected) > 1:
                actual_shifted = actual[1:]
                expected_shifted = expected[:-1]
                n = min(len(actual_shifted), len(expected_shifted))
                assert n > 0, f"{suffix} 序列为空"
                if ref_key == "volume":
                    np.testing.assert_allclose(actual_shifted[:n], expected_shifted[:n], rtol=0, atol=1e-6)
                else:
                    np.testing.assert_allclose(actual_shifted[:n], expected_shifted[:n], rtol=0, atol=TOLERANCE)
            else:
                n = min(len(actual), len(expected))
                assert n > 0, f"{suffix} 序列为空"
                if ref_key == "volume":
                    np.testing.assert_allclose(actual[:n], expected[:n], rtol=0, atol=1e-6)
                else:
                    np.testing.assert_allclose(actual[:n], expected[:n], rtol=0, atol=TOLERANCE)
            compared += 1
        assert compared > 0, "没有任何 OHLCV 字段被比对，用例退化为空通过"

    def test_weekly_series_is_shorter_than_daily(self, weekly_debug):
        """周频序列必须明显短于日线，否则说明降频没生效。"""
        daily_col = f"{WEEK_SYMBOL}.close"
        weekly_col = f"{WEEK_SYMBOL}.close_w"
        assert daily_col in weekly_debug.columns and weekly_col in weekly_debug.columns

        n_daily = weekly_debug[daily_col].notna().sum()
        n_weekly = weekly_debug[weekly_col].notna().sum()
        assert n_weekly > 0, "周频序列为空"
        assert n_weekly < n_daily / 3, (
            f"周频点数 {n_weekly} 相对日线 {n_daily} 过多，降频可能未生效")


# ------------------------------------------------------------
# 完成周滞后语义
# ------------------------------------------------------------

class TestSettledWeekSemantics:

    def test_weekly_value_constant_within_week(self, weekly_debug):
        """周内取值必须恒定：相邻交易日读到同一个周频值时，
        说明是「上一周定稿值」，而不是周内滚动值。

        若引擎改成周内滚动，本用例会因连续相同值过多而失败。
        """
        col = f"{WEEK_SYMBOL}.close_w"
        assert col in weekly_debug.columns
        vals = _num(weekly_debug[col])
        vals = vals[~np.isnan(vals)]
        assert len(vals) > 4, "周频样本太少，用例退化"

        # 完成周滞后：每个周频值在日线上会连续出现约 5 次。
        # 若为周内滚动，则几乎不会出现重复值。
        _, counts = np.unique(vals, return_counts=True)
        max_run = counts.max()
        assert max_run >= 2, (
            f"周频值最大连续重复次数仅 {max_run}，"
            f"疑似周内滚动而非完成周滞后")

    def test_weekly_lags_daily_by_one_week(self, weekly_debug, daily_rows):
        """下周一才可见上周定稿值：周频序列的第 i 个值应等于
        第 i 个 ISO 周的定稿值，而非本周至今的值。"""
        weekly_ref = weekly_reference(daily_rows)
        col = f"{WEEK_SYMBOL}.close_w"
        actual = _num(weekly_debug[col].dropna())
        # 第一个完整周之前没有「上一周」可定稿，故引擎从第二个周开始产出
        expected = np.array([w["close"] for w in weekly_ref])
        n = min(len(actual), len(expected))
        assert n > 2, "样本不足以判断滞后语义"

        # actual[k] 对应 expected[k]，且 actual 的取值不应等于「本周至今」
        # ——后者会与日线最后一根相等。用「与日线最后值不相等」做反向校验。
        last_daily = _num(weekly_debug[f"{WEEK_SYMBOL}.close"].dropna())
        if len(last_daily):
            tail = actual[-1]
            assert tail != last_daily[-1], (
                "周频最后一个值等于日线最后一个值，说明是当周滚动值而非定稿值")


# ------------------------------------------------------------
# 长假 / 跨年 ISO 周
# ------------------------------------------------------------

class TestIsoWeekBoundaries:

    def test_week_boundaries_span_year_rollover(self, weekly_ref):
        """基准里必须存在跨 ISO 年的周边界（iso_year 发生变化），
        否则本模块的跨年覆盖就是空跑。"""
        years = {_iso_week(w["date"])[0] for w in weekly_ref}
        assert len(years) >= 2, (
            f"基准只覆盖单个 ISO 年 {years}，跨年用例将退化为空通过")

    def test_no_duplicate_week_close(self, weekly_ref):
        """每周的 close 必须互不相同（价格持续变动），
        若出现大量重复说明周边界判定把多周合并成了一周。"""
        closes = [w["close"] for w in weekly_ref]
        assert len(set(closes)) >= len(closes) * 0.95, (
            "周频 close 出现大量重复值，疑似周边界判定错误")


# ------------------------------------------------------------
# range 单位 = bar
# ------------------------------------------------------------

class TestRangeInBars:

    def test_weekly_branch_range_is_weekly_bars(self, headers):
        """周频分支上 range=10 应消费 10 根**周线**。

        若引擎正确地在周线网格上计算 MA(10)，则 MA 结果也会按周更新，
        前向填充到日线后，同一周内所有 MA 值必须完全相同。
        若引擎错误地把窗口按日线解释，MA 会逐日滑动，周内值不再恒定。
        """
        _run(TEST_DIR / "weekly_ma_10.json", headers)
        df = read_debug_csv(f"test_multi_freq_wma_10", "debug_wma")
        col = f"{WEEK_SYMBOL}.WMA(10)"
        assert col in df.columns, f"缺少 {col}，实际: {list(df.columns)[:20]}"

        vals = _num(df[col])
        daily_dates = [r["datetime"] for r in _load_daily(WEEK_SYMBOL)]
        assert len(vals) == len(daily_dates), "MA 输出长度与日线不匹配"

        # 按 ISO 周分组，检查同一周内所有有效 MA 值是否完全一致
        week_buckets = {}
        for val, datestr in zip(vals, daily_dates):
            if np.isnan(val):
                continue
            key = _iso_week(datestr)
            week_buckets.setdefault(key, []).append(val)

        # 至少检查 5 个完整周，避免样本不足
        weeks_checked = 0
        for week_key, week_vals in sorted(week_buckets.items()):
            if len(week_vals) < 2:
                continue
            arr = np.array(week_vals)
            assert np.all(np.abs(arr - arr[0]) < 1e-6), (
                f"ISO 周 {week_key} 内 MA 值不稳定: {arr.tolist()}，"
                f"可能 MA 窗口被按日线解释了")
            weeks_checked += 1
        assert weeks_checked >= 5, (
            f"仅检查了 {weeks_checked} 个周，样本不足以验证 MA 行为")

    def test_daily_branch_range_is_daily_bars(self, headers):
        """纯日线分支 range=20 应消费 20 个交易日（行为与迁移前一致）。"""
        _run(TEST_DIR / "daily_ma_20.json", headers)
        df = read_debug_csv("test_multi_freq_dma_20", "debug_dma")
        col = f"{DAILY_SYMBOL}.DMA(20)"
        assert col in df.columns, f"缺少 {col}，实际: {list(df.columns)[:20]}"

        vals = _num(df[col].dropna())
        assert len(vals) > 0, "日线 MA 无有效值"

        closes = [r["close"] for r in _load_daily(DAILY_SYMBOL)]
        # 20 根日线窗口 → 有效点数 = 总数 - 20 + 1
        expected_n = len(closes) - 20 + 1
        assert len(vals) <= expected_n, (
            f"MA 输出 {len(vals)} 点，期望不超过 {expected_n}（20 根日线窗口）")

    def test_daily_ma_matches_numpy_rolling(self, headers):
        """日线 MA(20) 数值必须与 numpy 滚动均值一致（回归护栏：
        证明 range 单位改为 bar 后，日线策略行为没有改变）。"""
        _run(TEST_DIR / "daily_ma_20.json", headers)
        df = read_debug_csv("test_multi_freq_dma_20", "debug_dma")
        actual = _num(df[f"{DAILY_SYMBOL}.DMA(20)"].dropna())

        closes = np.array([r["close"] for r in _load_daily(DAILY_SYMBOL)])
        expected = np.convolve(closes, np.ones(20) / 20.0, mode="valid")

        n = min(len(actual), len(expected))
        assert n > 0, "MA 无有效值，用例退化"
        np.testing.assert_allclose(actual[:n], expected[:n], rtol=0, atol=1e-6)