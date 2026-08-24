#!/usr/bin/env python3
"""
从深交所（SZSE）官方获取 ETF 期权日终行情数据

数据源: 深圳证券交易所 http://www.szse.cn/
品种: 沪深300ETF(159919), 创业板ETF(159915), 中证500ETF(159922), 深证100ETF(159901)

覆盖范围: 各品种上市日 ~ 至今
  - 沪深300ETF 期权:  2019-12-23 起
  - 创业板ETF 期权:   2020-04-07 起
  - 中证500ETF 期权:  2022-09-19 起
  - 深证100ETF 期权:  2022-11-25 起

注: SZSE 官方日终数据通过 JSON API 返回，按日请求
    (akshare option_finance_* 走东财，深市期权数据不完整；本脚本走交易所补全)

下载约束: 看涨(C) 与 看跌(P) 必须同时下载, 不能分开
  - SZSE 单次 JSON API 返回深市所有 ETF 期权当日全部 call + put 合约
  - 本脚本不按 call_put 过滤, 全部保留在同一份 CSV 中
  - 若需按 call/put 拆分, 请在下游用 df[df.call_put=='认购'] 处理

用法:
  # 单日
  python fetch_szse_option.py --date 2024-01-15
  # 日期范围
  python fetch_szse_option.py --start 2024-01-15 --end 2024-01-31
  # 单一品种
  python fetch_szse_option.py --start 2024-01-15 --end 2024-01-31 --product 159919
  # 指定保存目录
  python fetch_szse_option.py --date 2024-01-15 --save-dir build/data/option_szse

输出:
  {save_dir}/{YYYY-MM-DD}.csv  每日一个 CSV（包含当日所有深市期权合约）
  列: trade_date, product, contract_code, contract_name, call_put,
      strike_price, open, high, low, close, settlement, prev_settlement,
      volume, turnover, open_interest
"""
import argparse
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

try:
    import requests
    import pandas as pd
except ImportError as e:
    print(f"错误: 缺少依赖 {e.name}: pip install requests pandas")
    sys.exit(1)


# 深交所 ETF 期权配置
SZSE_PRODUCTS = {
    "159919": {"name": "沪深300ETF期权",   "first_date": "2019-12-23"},
    "159915": {"name": "创业板ETF期权",    "first_date": "2020-04-07"},
    "159922": {"name": "中证500ETF期权",   "first_date": "2022-09-19"},
    "159901": {"name": "深证100ETF期权",   "first_date": "2022-11-25"},
}

# SZSE JSON API 配置
SZSE_API_URL = "http://www.szse.cn/api/report/ShowReport"
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    "Referer":    "http://www.szse.cn/option/",
    "Accept":     "application/json, text/javascript, */*; q=0.01",
}


def fetch_szse_daily(date_str: str) -> dict:
    """
    调用 SZSE JSON API 获取指定日期所有期权合约日终数据
    返回: API JSON 响应 (dict)
    """
    # 深交所 API 需要 txtDate 格式 YYYY-MM-DD
    # CATALOGID 不同版本可能不同
    catalog_ids = [
        "option_history",        # 历史日终数据
        "option_daily",          # 当日日终
        "history_opt_contract",  # 历史期权合约
    ]

    for catalog in catalog_ids:
        params = {
            "SHOWTYPE":  "JSON",
            "CATALOGID": catalog,
            "txtDate":   date_str,
            "random":    str(time.time()),
        }
        try:
            resp = requests.get(
                SZSE_API_URL, params=params, headers=HEADERS, timeout=20
            )
            if resp.status_code == 200:
                try:
                    data = resp.json()
                except ValueError:
                    continue
                # SZSE 响应通常有 data 字段含 records 或 data 列表
                if isinstance(data, dict) and ("data" in data or "records" in data):
                    return data
        except requests.RequestException as e:
            print(f"  [WARN] {date_str} API ({catalog}) 请求失败: {e}")
            continue
        time.sleep(0.3)
    return {}


def parse_szse_response(json_data: dict, date_str: str) -> pd.DataFrame:
    """
    解析 SZSE JSON 响应
    SZSE 字段（实际可能有差异，这里按常见模式映射）:
      secCode, secName, callOrPut, exercisePrice,
      openPrice, highestPrice, lowestPrice, closePrice,
      settlementPrice, prevSettlementPrice, volume, amount, openInterest
    """
    if not json_data:
        return pd.DataFrame()

    # 提取数据列表
    records = []
    if isinstance(json_data, list):
        records = json_data
    elif "data" in json_data:
        d = json_data["data"]
        if isinstance(d, list):
            records = d
        elif isinstance(d, dict) and "records" in d:
            records = d["records"]
    elif "records" in json_data:
        records = json_data["records"]

    if not records:
        return pd.DataFrame()

    # 字段映射（兼容多种命名风格）
    key_map = {
        "contract_code":  ["secCode", "contractCode", "code"],
        "contract_name":  ["secName", "contractName", "name"],
        "call_put":       ["callOrPut", "optType", "type"],
        "strike_price":   ["exercisePrice", "strikePrice", "execPrice"],
        "open":           ["openPrice", "open"],
        "high":           ["highestPrice", "high", "maxPrice"],
        "low":            ["lowestPrice", "low", "minPrice"],
        "close":          ["closePrice", "close"],
        "settlement":     ["settlementPrice", "settlePrice"],
        "prev_settlement": ["prevSettlementPrice", "prevSettlePrice"],
        "volume":         ["volume", "matchQty"],
        "turnover":       ["amount", "matchAmt", "turnover"],
        "open_interest":  ["openInterest", "holdQty", "position"],
    }

    normalized = []
    for r in records:
        if not isinstance(r, dict):
            continue
        norm = {"trade_date": date_str}
        for std_name, candidates in key_map.items():
            for c in candidates:
                if c in r:
                    val = r[c]
                    # callOrPut 字段可能为 "C"/"P" 或 "认购"/"认沽"
                    if std_name == "call_put" and isinstance(val, str):
                        if val in ("C", "认购", "CALL", "看涨"):
                            val = "认购"
                        elif val in ("P", "认沽", "PUT", "看跌"):
                            val = "认沽"
                    norm[std_name] = val
                    break
            else:
                norm[std_name] = pd.NA
        normalized.append(norm)

    df = pd.DataFrame(normalized)
    if df.empty:
        return df

    # 推断品种（按合约代码前缀）
    df["product"] = df["contract_code"].apply(
        lambda x: str(x)[:6] if pd.notna(x) and len(str(x)) >= 6 else ""
    )

    # 过滤只保留已知品种
    df = df[df["product"].isin(SZSE_PRODUCTS.keys())]

    output_cols = [
        "trade_date", "product", "contract_code", "contract_name", "call_put",
        "strike_price", "open", "high", "low", "close",
        "settlement", "prev_settlement", "volume", "turnover", "open_interest",
    ]
    for col in output_cols:
        if col not in df.columns:
            df[col] = pd.NA
    return df[output_cols]


def fetch_one_day(date_str: str, products: list, save_dir: Path) -> dict:
    """
    拉取单日 SZSE 期权数据并按品种保存
    返回: {product: row_count}
    """
    json_data = fetch_szse_daily(date_str)
    if not json_data:
        return {}

    df = parse_szse_response(json_data, date_str)
    if df.empty:
        return {}

    # 过滤品种
    df = df[df["product"].isin(products)]
    if df.empty:
        return {}

    # 按品种分别保存
    result = {}
    for prod in products:
        sub = df[df["product"] == prod]
        if sub.empty:
            continue
        out_path = save_dir / f"{date_str}_{prod}.csv"
        sub.to_csv(out_path, index=False, encoding="utf-8-sig")
        result[prod] = len(sub)

    # 合并版
    all_path = save_dir / f"{date_str}.csv"
    df.to_csv(all_path, index=False, encoding="utf-8-sig")
    result["_all"] = len(df)

    summary = ", ".join(f"{p}={n}" for p, n in result.items() if p != "_all")
    print(f"  [OK] {date_str}: {summary}")
    return result


def daterange(start: datetime, end: datetime):
    cur = start
    while cur <= end:
        yield cur
        cur += timedelta(days=1)


def main():
    parser = argparse.ArgumentParser(
        description="从深交所官方获取 ETF 期权日终行情（避开东财）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--date",       type=str, help="单日 YYYY-MM-DD")
    parser.add_argument("--start",      type=str, help="起始日期 YYYY-MM-DD")
    parser.add_argument("--end",        type=str, help="结束日期 YYYY-MM-DD")
    parser.add_argument("--product",    type=str,
                        choices=list(SZSE_PRODUCTS.keys()) + ["all"],
                        default="all",
                        help="ETF 期权标的代码 (159919/159915/159922/159901), 默认 all")
    parser.add_argument("--save-dir",   type=str, default="data/option_szse", help="CSV 保存目录")
    parser.add_argument("--skip-weekend", action="store_true", default=True, help="跳过周末（默认开）")
    args = parser.parse_args()

    if args.date:
        start = end = datetime.strptime(args.date, "%Y-%m-%d")
    elif args.start and args.end:
        start = datetime.strptime(args.start, "%Y-%m-%d")
        end = datetime.strptime(args.end, "%Y-%m-%d")
    else:
        print("错误: 必须指定 --date 或 (--start, --end)")
        sys.exit(1)

    save_dir = Path(args.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)
    products = list(SZSE_PRODUCTS.keys()) if args.product == "all" else [args.product]

    print(f"开始获取 SZSE ETF 期权: {start.date()} ~ {end.date()}, 品种: {products}")
    print(f"保存目录: {save_dir.absolute()}")
    print("=" * 70)

    success, fail, skipped = 0, 0, 0
    for d in daterange(start, end):
        date_str = d.strftime("%Y-%m-%d")
        if args.skip_weekend and d.weekday() >= 5:
            skipped += 1
            continue
        result = fetch_one_day(date_str, products, save_dir)
        if result:
            success += 1
        else:
            fail += 1
            print(f"  [WARN] {date_str} 无可用数据（节假日或未发布）")
        time.sleep(0.5)  # API 间隔

    print("=" * 70)
    print(f"完成: 成功 {success} 天, 失败 {fail} 天, 跳过 {skipped} 天（周末）")


if __name__ == "__main__":
    main()
