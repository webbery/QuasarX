"""
从 FRED API 下载汇率 / SOFR 数据

数据源: https://fred.stlouisfed.org
申请 API Key: https://fred.stlouisfed.org/docs/api/api_key.html (免费, 即时生效)

支持数据:
  汇率: EUR/USD, JPY/USD, CNY/USD, GBP/USD, HKD/USD, KRW/USD, AUD/USD, CAD/USD
  利率: SOFR (担保隔夜融资利率)

用法:
  python fetch_fred_data.py --api-key YOUR_KEY --type exchange_rate --save-dir ./data
  python fetch_fred_data.py --api-key YOUR_KEY --type sofr --start 2024-01-01
  python fetch_fred_data.py --api-key YOUR_KEY --type all --json
  python fetch_fred_data.py --api-key YOUR_KEY --series DFF --start 2023-01-01  # 自定义 series
"""
import argparse
import json
import csv
import sys
from datetime import datetime
from pathlib import Path

try:
    import requests
except ImportError:
    print("错误: 请先安装 requests 库: pip install requests")
    sys.exit(1)


# ============================================================
# FRED API 配置
# ============================================================

FRED_BASE_URL = "https://api.stlouisfed.org/fred"

# 预定义数据类别
SERIES_CATALOG = {
    # === 汇率 (每日即期汇率, 美元兑外币) ===
    "DEXUSEU": {"name": "USD/EUR 美元兑欧元", "category": "exchange_rate", "unit": "USD per EUR"},
    "DEXJPUS": {"name": "JPY/USD 日元兑美元", "category": "exchange_rate", "unit": "JPY per USD"},
    "DEXCHUS": {"name": "CNY/USD 人民币兑美元", "category": "exchange_rate", "unit": "CNY per USD"},
    "DEXHKUS": {"name": "HKD/USD 港币兑美元", "category": "exchange_rate", "unit": "HKD per USD"},
    "DEXKOUS": {"name": "KRW/USD 韩元兑美元", "category": "exchange_rate", "unit": "KRW per USD"},
    "DEXSDUS": {"name": "SEK/USD 瑞典克朗兑美元", "category": "exchange_rate", "unit": "SEK per USD"},
    "DEXMXUS": {"name": "MXN/USD 墨西哥比索兑美元", "category": "exchange_rate", "unit": "MXN per USD"},
    "DEXINUS": {"name": "INR/USD 印度卢比兑美元", "category": "exchange_rate", "unit": "INR per USD"},
    "DEXSFUS": {"name": "CHF/USD 瑞士法郎兑美元", "category": "exchange_rate", "unit": "CHF per USD"},
    "DEXUSUK": {"name": "USD/GBP 美元兑英镑", "category": "exchange_rate", "unit": "USD per GBP"},
    "DEXUSAL": {"name": "USD/AUD 美元兑澳元", "category": "exchange_rate", "unit": "USD per AUD"},
    "DEXUSCA": {"name": "USD/CAD 美元兑加元", "category": "exchange_rate", "unit": "USD per CAD"},
    "DEXBZUS": {"name": "BRL/USD 巴西雷亚尔兑美元", "category": "exchange_rate", "unit": "BRL per USD"},

    # === 利率 ===
    "SOFR": {"name": "SOFR 担保隔夜融资利率", "category": "rate", "unit": "Percent"},
    "DFF": {"name": "Federal Funds Effective Rate 联邦基金有效利率", "category": "rate", "unit": "Percent"},
    "DGS2": {"name": "2-Year Treasury Yield 2年期国债收益率", "category": "rate", "unit": "Percent"},
    "DGS5": {"name": "5-Year Treasury Yield 5年期国债收益率", "category": "rate", "unit": "Percent"},
    "DGS10": {"name": "10-Year Treasury Yield 10年期国债收益率", "category": "rate", "unit": "Percent"},
    "DGS30": {"name": "30-Year Treasury Yield 30年期国债收益率", "category": "rate", "unit": "Percent"},
    "T10Y2Y": {"name": "10Y-2Y Treasury Spread 10年-2年利差", "category": "rate", "unit": "Percent"},
    "T10Y3M": {"name": "10Y-3M Treasury Spread 10年-3月利差", "category": "rate", "unit": "Percent"},
    "TEDRATE": {"name": "TED Spread TED利差", "category": "rate", "unit": "Percent"},
}

# 按类别分组的 series ID
CATEGORY_SERIES = {
    "exchange_rate": [k for k, v in SERIES_CATALOG.items() if v["category"] == "exchange_rate"],
    "rate": [k for k, v in SERIES_CATALOG.items() if v["category"] == "rate"],
}


# ============================================================
# FRED API 请求
# ============================================================

def fetch_series(api_key: str, series_id: str, start: str = None, end: str = None) -> dict:
    """
    调用 FRED /series/observations 接口获取单个时间序列

    Args:
        api_key: FRED API Key
        series_id: 数据序列 ID (如 DFF, SOFR, DEXUSEU)
        start: 起始日期 YYYY-MM-DD
        end: 结束日期 YYYY-MM-DD

    Returns:
        dict: {"series_id": ..., "title": ..., "unit": ..., "observations": [...]}
    """
    url = f"{FRED_BASE_URL}/series/observations"
    params = {
        "series_id": series_id,
        "api_key": api_key,
        "file_type": "json",
    }
    if start:
        params["observation_start"] = start
    if end:
        params["observation_end"] = end

    response = requests.get(url, params=params, timeout=30)
    response.raise_for_status()
    data = response.json()

    if "error_message" in data:
        raise ValueError(f"FRED API 错误 ({series_id}): {data['error_message']}")

    # 获取 series 元信息
    meta_url = f"{FRED_BASE_URL}/series"
    meta_params = {
        "series_id": series_id,
        "api_key": api_key,
        "file_type": "json",
    }
    meta_resp = requests.get(meta_url, params=meta_params, timeout=15)
    meta_resp.raise_for_status()
    meta_data = meta_resp.json()

    title = ""
    unit = ""
    if "seriess" in meta_data and len(meta_data["seriess"]) > 0:
        title = meta_data["seriess"][0].get("title", "")
        unit = meta_data["seriess"][0].get("units", "")

    observations = data.get("observations", [])
    return {
        "series_id": series_id,
        "title": title,
        "unit": unit,
        "observations": observations,
    }


def fetch_multiple(api_key: str, series_ids: list, start: str = None, end: str = None, quiet: bool = False) -> list:
    """批量获取多个 series"""
    results = []
    for sid in series_ids:
        try:
            result = fetch_series(api_key, sid, start, end)
            valid = [o for o in result["observations"] if o.get("value") != "."]
            result["valid_count"] = len(valid)
            results.append(result)
            if not quiet:
                print(f"  ✓ {sid}: {len(valid)} 条有效数据")
        except Exception as e:
            if not quiet:
                print(f"  ✗ {sid}: {e}")
    return results


# ============================================================
# 数据输出
# ============================================================

def save_csv(results: list, filepath: Path):
    """
    保存为 CSV 格式

    格式: date, series_id, value
    """
    filepath = Path(filepath)
    filepath.parent.mkdir(parents=True, exist_ok=True)

    with open(filepath, "w", encoding="utf-8-sig", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(["date", "series_id", "value"])

        for result in results:
            sid = result["series_id"]
            for obs in result["observations"]:
                if obs.get("value") != ".":
                    writer.writerow([obs["date"], sid, obs["value"]])

    print(f"数据已保存到: {filepath}")


def save_json(results: list, filepath: Path):
    """保存为 JSON 格式"""
    filepath = Path(filepath)
    filepath.parent.mkdir(parents=True, exist_ok=True)

    output = {
        "updated_at": datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
        "series": [],
    }

    for result in results:
        series_data = {
            "series_id": result["series_id"],
            "title": result["title"],
            "unit": result["unit"],
            "observations": [
                {"date": o["date"], "value": float(o["value"])}
                for o in result["observations"]
                if o.get("value") != "."
            ],
        }
        output["series"].append(series_data)

    with open(filepath, "w", encoding="utf-8") as f:
        json.dump(output, f, ensure_ascii=False, indent=2)

    print(f"数据已保存到: {filepath}")


def print_table(results: list):
    """打印数据表格 (每个 series 打印最近 12 条)"""
    for result in results:
        sid = result["series_id"]
        title = result["title"] or SERIES_CATALOG.get(sid, {}).get("name", sid)
        unit = result["unit"]
        valid = [o for o in result["observations"] if o.get("value") != "."]

        print(f"\n{'=' * 70}")
        print(f"{title} ({sid})")
        print(f"{'=' * 70}")
        print(f"单位: {unit}  |  有效数据: {len(valid)} 条")
        print(f"{'-' * 70}")
        print(f"  {'日期':<14} {'值':>14}")
        print(f"  {'-' * 30}")

        for obs in valid[-12:]:
            try:
                val = float(obs["value"])
                print(f"  {obs['date']:<14} {val:>14.4f}")
            except (ValueError, TypeError):
                print(f"  {obs['date']:<14} {obs['value']:>14}")

        # 统计摘要
        if len(valid) > 0:
            values = []
            for o in valid:
                try:
                    values.append(float(o["value"]))
                except (ValueError, TypeError):
                    pass
            if values:
                print(f"  {'-' * 30}")
                print(f"  最新: {values[-1]:.4f}  |  均值: {sum(values)/len(values):.4f}  |  "
                      f"最小: {min(values):.4f}  |  最大: {max(values):.4f}")

    print()


def print_json_output(results: list):
    """JSON 模式输出 (供 C++ Handler / Agent Tool 解析)"""
    output = {
        "updated_at": datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
        "series": [],
    }

    for result in results:
        sid = result["series_id"]
        title = result["title"] or SERIES_CATALOG.get(sid, {}).get("name", sid)
        valid = [
            {"date": o["date"], "value": float(o["value"])}
            for o in result["observations"]
            if o.get("value") != "."
        ]
        output["series"].append({
            "series_id": sid,
            "name": title,
            "unit": result["unit"],
            "count": len(valid),
            "data": valid,
        })

    print(json.dumps(output, ensure_ascii=False, indent=2))


# ============================================================
# 列出支持的 series
# ============================================================

def list_series():
    """列出所有支持的 FRED series"""
    print(f"\n{'=' * 70}")
    print("FRED 支持的数据序列")
    print(f"{'=' * 70}")

    current_cat = None
    for sid, info in SERIES_CATALOG.items():
        cat = info["category"]
        if cat != current_cat:
            current_cat = cat
            cat_name = "汇率" if cat == "exchange_rate" else "利率"
            print(f"\n【{cat_name}】")
            print(f"{'-' * 70}")

        print(f"  {sid:<12} {info['name']:<40} [{info['unit']}]")

    print(f"\n{'=' * 70}")
    print(f"总计 {len(SERIES_CATALOG)} 个序列")
    print(f"\n用法:")
    print(f"  python fetch_fred_data.py --api-key YOUR_KEY --type exchange_rate")
    print(f"  python fetch_fred_data.py --api-key YOUR_KEY --type sofr")
    print(f"  python fetch_fred_data.py --api-key YOUR_KEY --series DFF DGS10")
    print(f"  python fetch_fred_data.py --api-key YOUR_KEY --type all --start 2024-01-01")
    print(f"{'=' * 70}\n")


# ============================================================
# 主流程
# ============================================================

def resolve_series_ids(args_type: str, args_series: list) -> list:
    """根据 --type 和 --series 参数确定要下载的 series ID 列表"""
    ids = []

    if args_series:
        ids.extend(args_series)

    if args_type == "exchange_rate":
        ids.extend(CATEGORY_SERIES["exchange_rate"])
    elif args_type == "rate":
        ids.extend(CATEGORY_SERIES["rate"])
    elif args_type == "sofr":
        ids.append("SOFR")
    elif args_type == "all":
        ids.extend(CATEGORY_SERIES["exchange_rate"])
        ids.extend(CATEGORY_SERIES["rate"])

    # 去重保序
    seen = set()
    unique = []
    for sid in ids:
        if sid not in seen:
            seen.add(sid)
            unique.append(sid)
    return unique


def main():
    parser = argparse.ArgumentParser(
        description="从 FRED API 下载汇率 / SOFR 数据",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python fetch_fred_data.py --api-key YOUR_KEY --type exchange_rate
  python fetch_fred_data.py --api-key YOUR_KEY --type sofr --start 2024-01-01
  python fetch_fred_data.py --api-key YOUR_KEY --type all --json
  python fetch_fred_data.py --api-key YOUR_KEY --series DFF DGS10 SOFR
  python fetch_fred_data.py --api-key YOUR_KEY --type exchange_rate --save-dir ./data --format csv
  python fetch_fred_data.py --list
        """,
    )
    parser.add_argument("--api-key", type=str, help="FRED API Key (必填, 免费申请: https://fred.stlouisfed.org/docs/api/api_key.html)")
    parser.add_argument("--type", type=str, default=None,
                        choices=["exchange_rate", "rate", "sofr", "all"],
                        help="数据类型: exchange_rate(汇率), rate(利率), sofr(SOFR), all(全部)")
    parser.add_argument("--series", nargs="+", type=str, help="自定义 series ID (如 DFF DGS10 SOFR)")
    parser.add_argument("--start", type=str, help="起始日期 (YYYY-MM-DD)")
    parser.add_argument("--end", type=str, help="结束日期 (YYYY-MM-DD)")
    parser.add_argument("--save-dir", type=str, default=None, help="保存目录 (不指定则只打印不保存)")
    parser.add_argument("--format", type=str, default="csv", choices=["csv", "json"], help="保存格式 (默认 csv)")
    parser.add_argument("--json", action="store_true", help="JSON 模式输出 (供程序解析, 不保存文件)")
    parser.add_argument("--list", action="store_true", help="列出所有支持的 series")

    args = parser.parse_args()

    if args.list:
        list_series()
        return

    if not args.api_key:
        print("错误: 请提供 --api-key 参数")
        print("免费申请: https://fred.stlouisfed.org/docs/api/api_key.html")
        parser.print_help()
        return

    if not args.type and not args.series:
        print("错误: 请指定 --type 或 --series 参数")
        parser.print_help()
        return

    # 确定要下载的 series
    series_ids = resolve_series_ids(args.type, args.series)
    if not series_ids:
        print("错误: 未指定任何要下载的数据序列")
        return

    # --json 模式只输出纯 JSON，不打印进度信息
    if not args.json:
        # 打印任务信息
        print(f"\n{'=' * 70}")
        print(f"FRED 数据下载")
        print(f"{'=' * 70}")
        print(f"数据序列: {', '.join(series_ids)}")
        print(f"日期范围: {args.start or '最早'} ~ {args.end or '最新'}")
        print(f"{'=' * 70}")

    # 下载数据
    if not args.json:
        print(f"\n正在下载...")
    results = fetch_multiple(args.api_key, series_ids, args.start, args.end, quiet=args.json)

    if not results:
        if not args.json:
            print("\n未获取到任何数据")
        return

    # 输出
    if args.json:
        print_json_output(results)
        return

    # 打印表格
    print_table(results)

    # 保存文件
    if args.save_dir:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        save_dir = Path(args.save_dir)

        if args.format == "csv":
            filepath = save_dir / f"fred_{timestamp}.csv"
            save_csv(results, filepath)
        else:
            filepath = save_dir / f"fred_{timestamp}.json"
            save_json(results, filepath)


if __name__ == "__main__":
    main()
