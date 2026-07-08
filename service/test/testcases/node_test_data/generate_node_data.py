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
import shutil
import numpy as np
from datetime import datetime, timedelta
from pathlib import Path

SERVICE_ROOT = Path(__file__).parent.parent.parent.parent
SERVICE_DATA_DIR = SERVICE_ROOT / "build" / "data"
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


def write_csv(symbol, prices, start_date, hfq_path, org_path, anomaly_bar=None):
    np.random.seed(42)
    with open(hfq_path, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['datetime', 'open', 'close', 'high', 'low', 'volume', 'turnover'])
        current_date = start_date
        for i, close in enumerate(prices):
            while current_date.weekday() >= 5:
                current_date += timedelta(days=1)
            date_str = current_date.strftime("%Y-%m-%d")

            if close <= 0:
                open_price = high = low = 0.0
            else:
                open_price = close * (1 + np.random.normal(0, 0.001))
                high = max(open_price, close) * (1 + abs(np.random.normal(0, 0.002)))
                low = min(open_price, close) * (1 - abs(np.random.normal(0, 0.002)))

            volume = 0 if (anomaly_bar is not None and i == anomaly_bar + 1) else int(np.random.uniform(1000000, 5000000))
            turnover = round(volume * close, 2) if close > 0 else 0.0

            writer.writerow([date_str, round(open_price, 2), round(close, 2),
                             round(high, 2), round(low, 2), volume, turnover])
            current_date += timedelta(days=1)
    shutil.copy2(hfq_path, org_path)


def make_strategy(strategy_id, nodes, edges, start_date, end_date, source="A_hfq"):
    return {
        "id": strategy_id,
        "name": f"节点测试_{strategy_id}",
        "version": 1,
        "description": "节点单元测试",
        "backtest": {
            "start": start_date.strftime("%Y-%m-%d"),
            "end": end_date.strftime("%Y-%m-%d")
        },
        "source": source,
        "nodes": nodes,
        "edges": _make_edges(edges)
    }


def _make_edges(edge_list):
    """edge_list 元素: (source, target) 或 (source, target, source_property)"""
    result = []
    for edge in edge_list:
        if len(edge) == 3:
            s, t, prop = edge
            sh = f"{s}-{prop}"
        else:
            s, t = edge
            sh = s
        result.append({"id": f"{sh}->{t}", "source": s, "target": t,
                       "sourceHandle": sh, "targetHandle": t, "type": "default"})
    return result


def input_node(nid, symbol, source="股票"):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": "行情数据", "nodeType": "input",
            "params": {
                "source": {"value": source, "type": "text"},
                "code": {"value": [symbol], "type": "text"},
                "freq": {"value": "1d", "type": "select"},
                "close": {"value": "close", "type": "text"},
                "open": {"value": "open", "type": "text"},
                "high": {"value": "high", "type": "text"},
                "low": {"value": "low", "type": "text"},
                "volume": {"value": "volume", "type": "text"}
            }
        }
    }


def function_node(nid, func_name, range_val, label=None):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": label or func_name, "nodeType": "function",
            "params": {
                "method": {"value": func_name, "type": "select"},
                "range": {"value": range_val, "type": "text"}
            }
        }
    }


def formula_node(nid, expression, label="Formula"):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": label, "nodeType": "formula",
            "params": {
                "expression": {"value": expression, "type": "text"}
            }
        }
    }


def debug_node(nid, label):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": label, "nodeType": "debug",
            "params": {"suffix": {"value": "csv", "type": "select"}}
        }
    }


def signal_node(nid, symbol):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": "买入信号", "nodeType": "signal",
            "params": {
                "code": {"value": [symbol], "type": "text"},
                "buy": {"value": "true", "type": "text"},
                "sell": {"value": "false", "type": "text"}
            }
        }
    }


def portfolio_node(nid):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": "投资组合", "nodeType": "portfolio",
            "params": {"positionRatio": {"value": 1.0, "type": "number"}}
        }
    }


def execution_node(nid):
    return {
        "id": nid, "type": "custom",
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
                "contract": {"value": 0, "type": "select"}
            }
        }
    }


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


def cusum_node(nid, mode="changepoint", min_obs=10):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": "CUSUM", "nodeType": "cusum",
            "params": {
                "mode": {"value": mode, "type": "select"},
                "lambda": {"value": 0.5, "type": "number"},
                "threshold_multiplier": {"value": 4.0, "type": "number"},
                "min_obs": {"value": min_obs, "type": "number"},
                "cooldown": {"value": 0, "type": "number"},
                "signal_label": {"value": "cusum", "type": "text"}
            }
        }
    }


def emd_node(nid, method="emd", num_imfs=5):
    return {
        "id": nid, "type": "custom",
        "position": {"x": 0, "y": 0},
        "data": {
            "label": "EMD", "nodeType": "emd",
            "params": {
                "method": {"value": method, "type": "select"},
                "numIMFs": {"value": num_imfs, "type": "number"}
            }
        }
    }


def cusum_strategy(symbol, n, dataset_id):
    """CUSUM 节点测试: Input → Return(1) → CUSUM → Debug + Signal/Portfolio/Execution"""
    strat_id = f"test_{dataset_id}_cusum_{n}"
    debug_label = f"debug_cusum_{n}"
    return make_strategy(
        strat_id,
        nodes=[
            input_node("1", symbol),
            function_node("2", "Return", f"{n}d", label=f"Return({n})"),
            cusum_node("3", min_obs=10),
            debug_node("4", debug_label),
            signal_node("5", symbol),
            portfolio_node("6"),
            execution_node("7"),
        ],
        edges=[
            ("1", "2", "close"),
            ("2", "3"),
            ("3", "4"),
            ("1", "5", "close"), ("5", "6"), ("6", "7"),
        ],
        start_date=START_DATE,
        end_date=START_DATE + timedelta(days=250)
    )


def emd_strategy(symbol, num_imfs, dataset_id):
    """EMD 节点测试: Input → EMD(close) → Debug + Signal/Portfolio/Execution"""
    strat_id = f"test_{dataset_id}_emd_{num_imfs}"
    debug_label = f"debug_emd_{num_imfs}"
    return make_strategy(
        strat_id,
        nodes=[
            input_node("1", symbol),
            emd_node("2", num_imfs=num_imfs),
            debug_node("3", debug_label),
            signal_node("4", symbol),
            portfolio_node("5"),
            execution_node("6"),
        ],
        edges=[
            ("1", "2", "close"),
            ("2", "3"),
            ("1", "4", "close"), ("4", "5"), ("5", "6"),
        ],
        start_date=START_DATE,
        end_date=START_DATE + timedelta(days=250)
    )


def formula_strategy(symbol, dataset_id, node_suffix, expression, upstream_specs, debug_label, connect_input=False):
    """Formula 测试策略

    upstream_specs: [(var_name, func_name, range_val), ...]
      - var_name: FormulaNode 中的引用名（同时也是上游节点 label 和 output key）
      - func_name: MA/STD/ZScore
      - range_val: "5d"/"15d"
    connect_input: 是否连接 InputNode → FormulaNode（用于 close/open 等原始行情引用）

    策略图: Input → [Upstream1, Upstream2, ...] → Formula → Debug + Signal → Portfolio → Execution
    """
    nodes = [input_node("1", symbol)]
    edges = [("1", "2", "close")]  # Input → 第一个 upstream

    next_id = 2
    upstream_ids = []
    for var_name, func_name, range_val in upstream_specs:
        nodes.append(function_node(str(next_id), func_name, range_val, label=var_name))
        upstream_ids.append(str(next_id))
        if len(upstream_ids) > 1:
            # 除第一个外，Input 也需要连接到
            edges.append(("1", str(next_id), "close"))
        next_id += 1

    # Formula 节点
    formula_id = str(next_id)
    nodes.append(formula_node(formula_id, expression, label=debug_label.replace("debug_", "")))
    for uid in upstream_ids:
        edges.append((uid, formula_id))
    # 如果需要连接 InputNode（如 close 引用）
    if connect_input:
        edges.append(("1", formula_id, "close"))
    next_id += 1

    # Debug 节点
    debug_id = str(next_id)
    nodes.append(debug_node(debug_id, debug_label))
    edges.append((formula_id, debug_id))
    next_id += 1

    # Signal/Portfolio/Execution (最小交易链)
    signal_id = str(next_id)
    nodes.append(signal_node(signal_id, symbol))
    edges.append(("1", signal_id, "close"))
    next_id += 1

    portfolio_id = str(next_id)
    nodes.append(portfolio_node(portfolio_id))
    edges.append((signal_id, portfolio_id))
    next_id += 1

    execution_id = str(next_id)
    nodes.append(execution_node(execution_id))
    edges.append((portfolio_id, execution_id))

    return make_strategy(
        f"test_{dataset_id}_{node_suffix}",
        nodes=nodes,
        edges=edges,
        start_date=START_DATE,
        end_date=START_DATE + timedelta(days=250)
    )


def single_node_strategy(symbol, dataset_id, node_suffix, func_name, range_val, debug_label):
    """通用单节点测试策略: Input → FunctionNode → Debug + Signal → Portfolio → Execution"""
    # label 只用数字窗口（不含 d 后缀），如 "STD(15)" 而非 "STD(15d)"
    import re
    window_num = re.match(r'(\d+)', range_val).group(1)
    node_label = f"{func_name}({window_num})"
    return make_strategy(
        f"test_{dataset_id}_{node_suffix}",
        nodes=[
            input_node("1", symbol),
            function_node("2", func_name, range_val, label=node_label),
            debug_node("3", debug_label),
            signal_node("4", symbol),
            portfolio_node("5"),
            execution_node("6"),
        ],
        edges=[
            ("1", "2", "close"), ("2", "3"),
            ("1", "4", "close"), ("4", "5"), ("5", "6"),
        ],
        start_date=START_DATE,
        end_date=START_DATE + timedelta(days=250)
    )


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
        write_csv(symbol, prices, START_DATE,
                  HFQ_DIR / f"{symbol}.csv", ORG_DIR / f"{symbol}.csv", anomaly_bar)
        print(f"  CSV: {len(prices)} bars")

        for gen_func, args, node_label in node_configs:
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

    summary = {"datasets": {k: {"symbol": v["symbol"], "name": v["name"]} for k, v in datasets.items()},
               "strategies": all_strategies}
    with open(TEST_DATA_DIR / "test_data_summary.json", 'w') as f:
        json.dump(summary, f, indent=2, ensure_ascii=False)
    print(f"\n汇总: test_data_summary.json")
    print(f"共 {len(datasets)} 数据集, {len(all_strategies)} 策略")


if __name__ == "__main__":
    main()
