#!/usr/bin/env python3
"""生成多频率行情输入（freq=1w 周线视图）的测试策略与基准数据。

周线由日线在引擎内按 ISO 周年周号聚合，不依赖独立的行情数据源，
因此测试数据只需日线 CSV。

基准口径与 service/lab/ahhmm/panel.py 一致：
  - groupby([iso_year, iso_week])
  - open  = 组内第一个交易日的 open
  - high  = 组内 high 的最大值
  - low   = 组内 low 的最小值
  - close = 组内最后一个交易日的 close
  - volume= 组内 volume 之和
  - 周标签 = 组内 datetime 的最大值（即该周最后一个交易日）
"""

import csv
import json
import math
import os
import random
import sys
from datetime import date, timedelta

_HERE = os.path.dirname(os.path.abspath(__file__))          # .../service/test/testcases/node_test_data
_TESTCASES = os.path.dirname(_HERE)                          # .../service/test/testcases

# 路径统一从 tool.py 解析（与 generate_node_data.py / generate_test_data.py 一致）。
# 不要自己手写探测：tool.py 的 find_service_root() + _resolve_data_dir() 认识 CI 布局
# （二进制在 repo root、数据在 repo_root/data），手写版本会漏掉这一支而写到 service/data。
sys.path.insert(0, _TESTCASES)
from tool import _DATA_DIR as SERVICE_DATA_DIR

CSV_DIR = os.path.join(str(SERVICE_DATA_DIR), "A_hfq")
OUT_DIR = _HERE  # 策略文件直接放在 node_test_data/ 下，与现有测试一致

WEEK_SYMBOL = "sz.800010"
DAILY_ONLY_SYMBOL = "sz.800011"
START = date(2024, 1, 1)
N_BARS = 520  # ~2 年交易日，覆盖跨年 ISO 周与春节长假


def trading_days(start: date, n: int):
    """生成 n 个交易日（周一~周五，不做节假日处理）。

    周频聚合的正确性不应依赖节假日日历：stepForward 只遍历数据里真实存在的
    bar，ISO 周边界由 bar 时间戳决定。测试数据刻意包含春节等长假，
    以验证 ISO 周断裂时不错位。
    """
    days, d = [], start
    while len(days) < n:
        if d.weekday() < 5:
            days.append(d)
        d += timedelta(days=1)
    return days


def gen_price_series(days, seed=20260101):
    """生成确定性 OHLCV，便于逐值比对。"""
    rnd = random.Random(seed)
    rows, price = [], 10.0
    for i, d in enumerate(days):
        drift = math.sin(i / 7.0) * 0.15 + math.cos(i / 23.0) * 0.08
        open_ = price
        close = max(1.0, open_ + drift + rnd.uniform(-0.1, 0.1))
        high = max(open_, close) + abs(rnd.uniform(0.0, 0.2))
        low = min(open_, close) - abs(rnd.uniform(0.0, 0.2))
        volume = 100000 + (i % 7) * 1000
        rows.append({
            "datetime": d.strftime("%Y-%m-%d"),
            "open": round(open_, 4), "close": round(close, 4),
            "high": round(high, 4), "low": round(low, 4),
            "volume": volume,
        })
        price = close
    return rows


def write_csv(symbol, rows):
    os.makedirs(CSV_DIR, exist_ok=True)
    path = os.path.join(CSV_DIR, f"{symbol}.csv")
    # 添加 turnover 列，与现有测试数据格式一致
    for row in rows:
        row["turnover"] = row["volume"] * row["close"]  # 简单估算
    with open(path, "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=["datetime", "open", "close", "high", "low", "volume", "turnover"])
        w.writeheader()
        w.writerows(rows)
    print(f"  日线数据: {path} ({len(rows)} bars)")
    return path


# ------------------------------------------------------------
# 策略构造
# ------------------------------------------------------------

def _edges(edge_list):
    out = []
    for e in edge_list:
        if len(e) == 3:
            s, t, prop = e
            sh = f"{s}-{prop}"
        else:
            s, t = e
            sh = s
        out.append({"id": f"{sh}->{t}", "source": s, "target": t,
                    "sourceHandle": sh, "targetHandle": t, "type": "default"})
    return out


def input_node(nid, symbol, freq="1d"):
    return {
        "id": nid, "type": "custom", "position": {"x": 0, "y": 0},
        "data": {
            "label": f"行情_{freq}", "nodeType": "input",
            "params": {
                "source": {"value": "股票", "type": "text"},
                "code": {"value": [symbol], "type": "text"},
                "freq": {"value": freq, "type": "select"},
                "close": {"value": "close", "type": "text"},
                "open": {"value": "open", "type": "text"},
                "high": {"value": "high", "type": "text"},
                "low": {"value": "low", "type": "text"},
                "volume": {"value": "volume", "type": "text"},
            },
        },
    }


def debug_node(nid, label):
    return {
        "id": nid, "type": "custom", "position": {"x": 0, "y": 0},
        "data": {"label": label, "nodeType": "debug",
                 "params": {"suffix": {"value": "csv", "type": "select"}}},
    }


def function_node(nid, method, window, label):
    return {
        "id": nid, "type": "custom", "position": {"x": 0, "y": 0},
        "data": {"label": label, "nodeType": "function",
                 "params": {"method": {"value": method, "type": "select"},
                            "range": {"value": window, "type": "number"}}},
    }


def signal_node(nid, symbol):
    return {
        "id": nid, "type": "custom", "position": {"x": 0, "y": 0},
        "data": {"label": "买入信号", "nodeType": "signal",
                 "params": {"code": {"value": [symbol], "type": "text"},
                            "buy": {"value": "true", "type": "text"},
                            "sell": {"value": "false", "type": "text"}}},
    }


def portfolio_node(nid):
    return {
        "id": nid, "type": "custom", "position": {"x": 0, "y": 0},
        "data": {"label": "投资组合", "nodeType": "portfolio",
                 "params": {"positionRatio": {"value": 1.0, "type": "number"}}},
    }


def execution_node(nid):
    return {
        "id": nid, "type": "custom", "position": {"x": 0, "y": 0},
        "data": {"label": "执行交易", "nodeType": "execution",
                 "params": {"commission": {"value": 0.0003, "type": "number"},
                            "stampDuty": {"value": 0.001, "type": "number"}}},
    }


def make_strategy(strategy_id, nodes, edges, symbol, start, end):
    return {
        "id": strategy_id, "name": f"多频率测试_{strategy_id}",
        "version": 1, "description": "多频率行情输入（周线视图）测试",
        "backtest": {"start": start.strftime("%Y-%m-%d"),
                     "end": end.strftime("%Y-%m-%d")},
        "source": "A_hfq", "nodes": nodes, "edges": _edges(edges),
    }


def weekly_view_strategy(symbol=WEEK_SYMBOL):
    """日线输入 + 周线输入并存，两个 Debug 分别导出序列。"""
    return make_strategy(
        "test_multi_freq_weekly",
        nodes=[
            input_node("1", symbol, freq="1d"),
            input_node("2", symbol, freq="1w"),
            debug_node("3", "debug_weekly"),
            signal_node("4", symbol),
            portfolio_node("5"),
            execution_node("6"),
        ],
        edges=[("1", "3", "close"), ("2", "3"), ("1", "4", "close"), ("4", "5"), ("5", "6")],
        symbol=symbol, start=START, end=START + timedelta(days=800),
    )


def weekly_ma_strategy(symbol=WEEK_SYMBOL, window=10):
    """周线分支上的窗口指标：range=10 应表示 10 周而非 10 个交易日。"""
    return make_strategy(
        f"test_multi_freq_wma_{window}",
        nodes=[
            input_node("1", symbol, freq="1w"),
            function_node("2", "MA", window, f"WMA({window})"),
            debug_node("3", "debug_wma"),
            signal_node("4", symbol),
            portfolio_node("5"),
            execution_node("6"),
        ],
        edges=[("1", "2", "close_w"), ("2", "3"), ("1", "4", "close"), ("4", "5"), ("5", "6")],
        symbol=symbol, start=START, end=START + timedelta(days=800),
    )


def daily_ma_strategy(symbol=DAILY_ONLY_SYMBOL, window=20):
    """纯日线分支：range=20 是 20 个交易日。"""
    return make_strategy(
        f"test_multi_freq_dma_{window}",
        nodes=[
            input_node("1", symbol, freq="1d"),
            function_node("2", "MA", window, f"DMA({window})"),
            debug_node("3", "debug_dma"),
            signal_node("4", symbol),
            portfolio_node("5"),
            execution_node("6"),
        ],
        edges=[("1", "2", "close"), ("2", "3"), ("1", "4", "close"), ("4", "5"), ("5", "6")],
        symbol=symbol, start=START, end=START + timedelta(days=800),
    )


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    days = trading_days(START, N_BARS)

    print("生成测试数据...")
    write_csv(WEEK_SYMBOL, gen_price_series(days, seed=20260101))
    write_csv(DAILY_ONLY_SYMBOL, gen_price_series(days, seed=20260202))

    strategies = {
        "weekly.json": weekly_view_strategy(),
        "weekly_ma_10.json": weekly_ma_strategy(window=10),
        "daily_ma_20.json": daily_ma_strategy(window=20),
    }
    for fname, strat in strategies.items():
        path = os.path.join(OUT_DIR, fname)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(strat, f, ensure_ascii=False, indent=2)
        print(f"  策略: {path}")

    # 基准：日线 CSV 的 ISO 周聚合
    bench_path = os.path.join(OUT_DIR, "weekly_benchmark.json")
    with open(bench_path, "w", encoding="utf-8") as f:
        json.dump({"symbol": WEEK_SYMBOL, "rows": gen_price_series(days, seed=20260101)}, f)
    print(f"  基准: {bench_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())