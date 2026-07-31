#!/usr/bin/env python3
"""
测试 /v0/strategy/simulate/bar 端点

用法：
    python test_simulate_bar.py <strategy_json_path> [--date YYYY-MM-DD]

示例：
    python test_simulate_bar.py ../build/scripts/cta_v13.json
    python test_simulate_bar.py ../build/scripts/cta_v13.json --date 2025-12-29

注意：/v0/strategy/simulate/bar 仅在 Debug 构建下可用
"""

import argparse
import json
import sys
import time
from pathlib import Path

import requests
import urllib3

# 抑制自签名证书警告
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False  # 自签名证书，跳过验证


def get_auth_token() -> str:
    """获取认证 token"""
    resp = requests.post(
        f"{BASE_URL}/user/login",
        json={"name": "admin", "pwd": "admin"},
        verify=VERIFY_SSL,
    )
    resp.raise_for_status()
    return resp.json()["tk"]


def load_strategy(path: str) -> dict:
    """加载策略 JSON"""
    with open(path, "r", encoding="utf-8") as f:
        return json.load(f)


def extract_symbols(strategy: dict) -> list[str]:
    """从策略的 input 节点提取标的列表"""
    for node in strategy.get("nodes", []):
        if node.get("data", {}).get("nodeType") == "input":
            code_param = node["data"]["params"].get("code", {})
            return code_param.get("value", [])
    return []


def get_latest_bar(symbol: str, date: str = None) -> dict:
    """从 QuoteDB 或 CSV 获取最新 bar 数据"""
    # 尝试从 API 获取最新行情
    try:
        resp = requests.get(
            f"{BASE_URL}/stocks/quote",
            params={"symbol": symbol},
            verify=VERIFY_SSL,
        )
        if resp.ok:
            data = resp.json()
            if data:
                return {
                    "symbol": symbol,
                    "open": data.get("open", 0),
                    "high": data.get("high", 0),
                    "low": data.get("low", 0),
                    "close": data.get("close", 0),
                    "volume": data.get("volume", 0),
                    "datetime": date or data.get("datetime", ""),
                }
    except Exception:
        pass

    # 回退：从 CSV 读取最后一行
    csv_path = Path(f"../build/data/A_hfq/{symbol}.csv")
    if csv_path.exists():
        with open(csv_path, "r") as f:
            lines = f.readlines()
            if len(lines) > 1:
                header = lines[0].strip().split(",")
                last_row = lines[-1].strip().split(",")
                row_dict = dict(zip(header, last_row))
                return {
                    "symbol": symbol,
                    "open": float(row_dict.get("open", 0)),
                    "high": float(row_dict.get("high", 0)),
                    "low": float(row_dict.get("low", 0)),
                    "close": float(row_dict.get("close", 0)),
                    "volume": int(float(row_dict.get("volume", 0))),
                    "datetime": date or row_dict.get("datetime", ""),
                }

    raise ValueError(f"Cannot get bar data for {symbol}")


def simulate_bar(token: str, bar: dict) -> dict:
    """POST /v0/strategy/simulate/bar"""
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy/simulate/bar",
        json=bar,
        headers=headers,
        verify=VERIFY_SSL,
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()


def load_strategy(token: str, name: str, script: dict) -> dict:
    """POST /v0/strategy action=load — 加载策略到 StrategySubSystem（不保存文件、不 Run）"""
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "load", "name": name, "script": script},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()


def get_decisions(date: str) -> dict:
    """获取指定日期的决策文件"""
    decisions_path = Path(f"../build/data/decisions/{date}.json")
    if decisions_path.exists():
        with open(decisions_path, "r", encoding="utf-8") as f:
            return json.load(f)
    return {}


def wait_for_decisions(date: str, timeout: int = 60) -> dict:
    """等待决策文件生成"""
    start = time.time()
    while time.time() - start < timeout:
        decisions = get_decisions(date)
        if decisions:
            return decisions
        time.sleep(1)
    return {}


def main():
    parser = argparse.ArgumentParser(description="测试 simulate/bar 端点")
    parser.add_argument("strategy", help="策略 JSON 文件路径")
    parser.add_argument("--date", help="目标日期 (YYYY-MM-DD)，默认当天")
    parser.add_argument("--timeout", type=int, default=60, help="等待决策超时秒数")
    args = parser.parse_args()

    # 加载策略
    strategy = load_strategy(args.strategy)
    strategy_name = strategy.get("id", Path(args.strategy).stem)
    symbols = extract_symbols(strategy)

    print(f"策略: {strategy_name}")
    print(f"标的: {symbols}")

    if not symbols:
        print("错误: 未找到标的")
        sys.exit(1)

    # 获取 token
    token = get_auth_token()
    print(f"Token: {token[:20]}...")

    # 加载策略到 StrategySubSystem
    print(f"\n=== 加载策略 ===")
    strategy_filename = Path(args.strategy).name
    result = load_strategy(token, strategy_filename, strategy)
    print(f"  {result.get('message', 'unknown')}: {result.get('name', '')}")

    # 确定日期
    date = args.date or time.strftime("%Y-%m-%d")
    print(f"日期: {date}")

    # 为每个标的注入 bar
    print("\n=== 注入 Bar 数据 ===")
    for symbol in symbols:
        try:
            bar = get_latest_bar(symbol, date)
            print(f"  {symbol}: close={bar['close']:.2f}")
            result = simulate_bar(token, bar)
            print(f"    → {result.get('status', 'unknown')}: {result.get('message', '')}")
        except Exception as e:
            print(f"  {symbol}: 错误 - {e}")

    # 等待策略执行
    print(f"\n=== 等待决策 (最多 {args.timeout}s) ===")
    decisions = wait_for_decisions(date, args.timeout)

    if decisions:
        print(f"\n=== 决策结果 ===")
        strategies = decisions.get("strategies", {})
        for name, data in strategies.items():
            print(f"\n策略: {name}")
            print(f"  状态: {data.get('status', 'unknown')}")
            for d in data.get("decisions", []):
                sym = d.get("symbol", "?")
                action = d.get("action", "HOLD")
                qty = d.get("quantity", 0)
                price = d.get("price", 0)
                print(f"  {sym}: {action} qty={qty} price={price:.2f}")
    else:
        print("超时: 未收到决策")
        sys.exit(1)

    print("\n测试完成")


if __name__ == "__main__":
    main()
