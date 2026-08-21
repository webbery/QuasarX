#!/usr/bin/env python3
"""
通用策略执行工具 — 通过 C++ 后端 API 回测任意策略 JSON

用法:
  # 回测 cta_v16.json
  python run_strategy.py ../../cta_v16.json

  # 指定服务地址
  python run_strategy.py ../../cta_v16.json --url https://localhost:19107

  # 保存完整结果
  python run_strategy.py ../../cta_v16.json --save result.json

  # 作为模块导入
  from run_strategy import StrategyRunner
  runner = StrategyRunner()
  data = runner.backtest("path/to/strategy.json")
"""

import argparse
import json
import sys
import time
from datetime import datetime
from pathlib import Path

import numpy as np
import requests
import urllib3

urllib3.disable_warnings()

SCRIPT_DIR = Path(__file__).parent
SERVICE_ROOT = SCRIPT_DIR.parent.parent
REPO_ROOT = SERVICE_ROOT.parent


class StrategyRunner:
    """策略执行器：封装登录 + 回测 API 调用"""

    def __init__(self, base_url: str = "https://localhost:19107",
                 name: str = "admin", pwd: str = "admin"):
        self.base_url = base_url
        self._name = name
        self._pwd = pwd
        self._token = None

    def login(self) -> str:
        for i in range(10):
            try:
                resp = requests.post(
                    f"{self.base_url}/v0/user/login",
                    json={"name": self._name, "pwd": self._pwd},
                    verify=False, timeout=5,
                )
                data = resp.json()
                token = data.get("tk", "")
                if token:
                    self._token = token
                    return token
            except Exception as e:
                if i < 9:
                    time.sleep(2)
                else:
                    raise ConnectionError(f"登录失败: {e}")
        raise ConnectionError("登录失败（重试 10 次）")

    @property
    def headers(self):
        if not self._token:
            self.login()
        return {"Authorization": self._token}

    def deploy(self, strategy: dict, strategy_name: str = None,
               model_base_dir: str = None) -> dict:
        """multipart 部署策略 + 模型（对齐 test_strategy.py TestStrategyMultipartDeploy）

        扫描策略中的 XGBoost 节点，从 models/ 目录查找模型文件上传到 production/。
        model_base_dir: 模型文件的本地搜索根目录（默认 SERVICE_ROOT/build/data）
        """
        if model_base_dir is None:
            model_base_dir = str(SERVICE_ROOT / "build" / "data")

        name = strategy_name or strategy.get("name", "unnamed")
        files = {
            "script": ("script.json",
                        json.dumps(strategy, ensure_ascii=False).encode("utf-8"),
                        "application/json"),
        }

        for node in strategy.get("nodes", []):
            nt = node.get("data", {}).get("nodeType", "")
            if nt != "xgboost":
                continue
            label = node.get("data", {}).get("label", nt)
            model_file = node.get("data", {}).get("params", {}).get(
                "modelFile", {}).get("value", "")
            if not model_file:
                continue

            # 从 modelFile 提取文件名，在 models/ 目录查找实际文件
            # modelFile 格式: "production/{name}-{label}.json"
            # 本地文件名可能只是 "{label}.json" 或原始训练文件名
            fname = Path(model_file).name  # e.g. "CTA_v16-CTA_v16.json"
            models_dir = Path(model_base_dir) / "models"
            local_path = models_dir / fname
            if not local_path.exists():
                # 尝试只用 label 名查找（e.g. "CTA_v16.json"）
                local_path = models_dir / f"{label}.json"
            if not local_path.exists():
                # 兜底：直接拼路径（兼容旧格式 "data/models/xxx.json"）
                rel = model_file
                if rel.startswith("data/") or rel.startswith("data\\"):
                    rel = rel[5:]
                local_path = Path(model_base_dir) / rel
            if not local_path.exists():
                print(f"  ⚠ 模型文件不存在: {local_path}，跳过上传")
                continue

            with open(local_path, "rb") as f:
                model_bytes = f.read()
            files[f"model_{label}"] = (f"{label}.json", model_bytes, "application/json")

            meta_path = local_path.with_suffix(".meta.json")
            if meta_path.exists():
                with open(meta_path, "rb") as f:
                    files[f"model_{label}_meta"] = (
                        f"{label}.meta.json", f.read(), "application/json")
            print(f"  模型: {label} ← {local_path.name}")

        resp = requests.post(
            f"{self.base_url}/v0/strategy",
            files=files,
            data={"name": name},
            headers=self.headers,
            verify=False,
            timeout=60,
        )
        if resp.status_code != 200:
            return {"status": "error", "error": f"HTTP {resp.status_code}: {resp.text[:300]}"}
        return resp.json()

    def import_data(self, strategy: dict, data_dir: str = None) -> int:
        """从 CSV 导入策略引用的标的数据到 DuckDB（对齐 conftest.py upload_test_data）"""
        if data_dir is None:
            data_dir = str(SERVICE_ROOT / "build" / "data")

        # 从策略中提取标的
        symbols = []
        for node in strategy.get("nodes", []):
            if node.get("data", {}).get("nodeType") == "input":
                codes = node["data"]["params"].get("code", {}).get("value", [])
                symbols.extend(codes)
        symbols = list(set(symbols))
        if not symbols:
            return 0

        imported = 0
        hfq_dir = Path(data_dir) / "A_hfq"
        for sym in symbols:
            csv_path = hfq_dir / f"{sym}.csv"
            if not csv_path.exists():
                print(f"  ⚠ {sym}: CSV 不存在 {csv_path}")
                continue
            with open(csv_path) as f:
                lines = f.read().strip().split("\n")
            if len(lines) < 2:
                continue
            # 补齐 turnover 列（如果缺失）
            if "turnover" not in lines[0].lower():
                lines = [lines[0] + ",turnover"] + [l + ",0" for l in lines[1:]]
            try:
                resp = requests.post(
                    f"{self.base_url}/v0/quote/data",
                    json={"action": "import", "table": "stock_1d",
                          "symbol": sym, "data": lines, "data_hfq": lines},
                    headers=self.headers, verify=False, timeout=30,
                )
                if resp.status_code == 200:
                    imported += 1
                    print(f"  ✓ {sym}: {len(lines)-1} bars")
                else:
                    print(f"  ✗ {sym}: HTTP {resp.status_code}")
            except Exception as e:
                print(f"  ✗ {sym}: {e}")
        return imported

    def backtest(self, strategy_path: str, timeout: int = 600,
                 deploy_first: bool = True) -> dict:
        """加载策略 JSON，导入数据 + 部署模型 + 回测"""
        path = Path(strategy_path)
        if not path.exists():
            raise FileNotFoundError(f"策略文件不存在: {path}")

        with open(path) as f:
            strategy = json.load(f)

        # 1. 导入数据
        print("  导入行情数据...")
        n = self.import_data(strategy)
        if n == 0:
            print("  ⚠ 无数据导入，回测可能无结果")

        # 2. 部署模型
        if deploy_first:
            name = strategy.get("name", "unnamed")
            print(f"  部署模型 (策略: {name})...")
            result = self.deploy(strategy, strategy_name=name)
            if result.get("status") == "error":
                print(f"  ⚠ 部署失败: {result['error']}")
                print("  继续尝试回测...")

        return self.backtest_dict(strategy, timeout=timeout)

    def backtest_dict(self, strategy: dict, timeout: int = 600) -> dict:
        """直接提交策略 dict 回测（multipart 文件上传，对齐 test_strategy.py）"""
        script_json = json.dumps(strategy, ensure_ascii=False)
        headers = dict(self.headers)
        files = {
            "script": ("script.json", script_json.encode("utf-8"), "application/json"),
        }
        resp = requests.post(
            f"{self.base_url}/v0/backtest",
            files=files,
            headers=headers,
            verify=False,
            timeout=timeout,
        )
        if resp.status_code != 200:
            print(f"回测失败: HTTP {resp.status_code}")
            print(resp.text[:500])
            return {"status": "error", "error": f"HTTP {resp.status_code}: {resp.text[:300]}"}
        return resp.json()

    def print_result(self, data: dict, show_trades: bool = True):
        """打印回测结果"""
        _print_summary(data)
        if show_trades:
            _print_trades(data)
        _print_daily_returns(data)


def _fmt_ts(ts: int) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d")


def _print_summary(data: dict):
    summary = data.get("summary", {})
    if not summary:
        print("⚠ 无 summary")
        return

    print("\n" + "=" * 60)
    name = summary.get("strategy_name", "")
    print(f"  回测结果{' — ' + name if name else ''}")
    print("=" * 60)

    rows = [
        ("Sharpe", summary.get("sharp"), ".4f"),
        ("Annual Return", summary.get("annual_return"), "%"),
        ("Total Return", summary.get("total_return"), "%"),
        ("Max Drawdown", summary.get("max_drawdown"), "%"),
        ("Win Rate", summary.get("win_rate"), "%"),
        ("Calmar", summary.get("calmar_ratio"), ".4f"),
        ("Buy Count", summary.get("buy_count"), "d"),
        ("Sell Count", summary.get("sell_count"), "d"),
    ]
    print(f"\n{'指标':<18} {'值':>12}")
    print("-" * 33)
    for label, val, fmt in rows:
        if val is None:
            print(f"{label:<18} {'N/A':>12}")
        elif fmt == "%":
            print(f"{label:<18} {val:>11.2%}")
        elif fmt == "d":
            print(f"{label:<18} {val:>12d}")
        else:
            print(f"{label:<18} {val:>12{fmt}}")

    # 额外字段
    skip = {k for k, _, _ in rows} | {"strategy_name"}
    extra = {k: v for k, v in summary.items() if k not in skip}
    if extra:
        print(f"\n{'其他':}")
        for k, v in sorted(extra.items()):
            print(f"  {k:<18} {v}")


def _print_trades(data: dict, max_show: int = 30):
    buys = data.get("buy", [])
    sells = data.get("sell", [])
    if not buys and not sells:
        print("\n⚠ 无交易记录")
        return

    print(f"\n{'=' * 60}")
    print(f"  交易记录 (买 {len(buys)} / 卖 {len(sells)})")
    print(f"{'=' * 60}")

    all_trades = []
    for t in buys:
        all_trades.append({**t, "side": "BUY"})
    for t in sells:
        all_trades.append({**t, "side": "SELL"})
    all_trades.sort(key=lambda x: x.get("dt", 0))

    print(f"\n{'日期':<12} {'方向':<5} {'标的':<14} {'价格':>10} {'数量':>8}")
    print("-" * 52)
    for t in all_trades[:max_show]:
        dt = _fmt_ts(t["dt"]) if t.get("dt") else "?"
        print(f"{dt:<12} {t['side']:<5} {t.get('symb','?'):<14} "
              f"{t.get('p',0):>10.2f} {t.get('q',0):>8}")
    if len(all_trades) > max_show:
        print(f"  ... 共 {len(all_trades)} 笔，显示前 {max_show}")


def _print_daily_returns(data: dict):
    rets = data.get("daily_returns", [])
    dates = data.get("daily_dates", [])
    if not rets:
        return

    arr = np.array(rets)
    valid = arr[~np.isnan(arr)]
    if len(valid) == 0:
        return

    print(f"\n{'=' * 60}")
    print(f"  日收益率 ({len(valid)} 个交易日)")
    print(f"{'=' * 60}")

    if dates and len(dates) == len(rets):
        valid_dates = [d for d, r in zip(dates, rets) if not np.isnan(r)]
        if valid_dates:
            print(f"  区间: {_fmt_ts(valid_dates[0])} ~ {_fmt_ts(valid_dates[-1])}")

    pos = int(np.sum(valid > 0))
    neg = int(np.sum(valid < 0))
    cum = float(np.cumprod(1 + valid)[-1] - 1)

    print(f"\n  均值:   {np.mean(valid):>10.4%}")
    print(f"  标准差: {np.std(valid):>10.4%}")
    print(f"  最大:   {np.max(valid):>10.4%}")
    print(f"  最小:   {np.min(valid):>10.4%}")
    print(f"  正/负:  {pos}/{neg}")
    print(f"  累计:   {cum:>10.2%}")


def main():
    parser = argparse.ArgumentParser(
        description="通用策略回测工具",
        usage="python run_strategy.py <strategy.json> [options]",
    )
    parser.add_argument("strategy", help="策略 JSON 文件路径")
    parser.add_argument("--url", default="https://localhost:19107", help="服务地址")
    parser.add_argument("--timeout", type=int, default=600, help="超时秒数")
    parser.add_argument("--save", default="", help="保存结果到 JSON")
    parser.add_argument("--no-trades", action="store_true", help="不打印交易记录")
    args = parser.parse_args()

    # 解析路径（支持相对路径）
    strategy_path = Path(args.strategy)
    if not strategy_path.is_absolute():
        strategy_path = (Path.cwd() / strategy_path).resolve()

    # 打印策略信息
    with open(strategy_path) as f:
        strategy = json.load(f)

    print(f"[策略] {strategy_path.name}")
    print(f"  名称: {strategy.get('name', '?')}")
    print(f"  节点: {len(strategy.get('nodes', []))}  边: {len(strategy.get('edges', []))}")
    bt = strategy.get("backtest", {})
    if bt:
        print(f"  回测: {bt.get('start', '?')} ~ {bt.get('end', '?')}")
    for node in strategy.get("nodes", []):
        if node.get("data", {}).get("nodeType") == "input":
            codes = node["data"]["params"].get("code", {}).get("value", [])
            print(f"  标的: {codes}")
            break

    # 执行回测
    runner = StrategyRunner(base_url=args.url)
    print(f"\n[连接] {args.url}")
    runner.login()
    print("  登录成功")

    print(f"\n[回测] 部署模型 + 提交回测...")
    t0 = time.time()
    data = runner.backtest(str(strategy_path), timeout=args.timeout, deploy_first=True)
    print(f"  完成 ({time.time() - t0:.1f}s)")

    # 输出结果
    runner.print_result(data, show_trades=not args.no_trades)

    # 保存
    if args.save:
        with open(args.save, "w") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print(f"\n[保存] → {args.save}")

    print()


if __name__ == "__main__":
    main()
