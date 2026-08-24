#!/usr/bin/env python3
"""
从上交所（SSE）官方获取 ETF 期权日终行情数据

数据源: 上海证券交易所 http://www.sse.com.cn/
品种: 50ETF(510050), 沪深300ETF(510300), 500ETF(510500), 科创50ETF(588000)

覆盖范围: 各品种上市日 ~ 至今
  - 50ETF 期权:       2015-02-09 起 (10 年)
  - 沪深300ETF 期权:  2019-12-23 起
  - 500ETF 期权:      2022-09-19 起
  - 科创50ETF 期权:   2023-06-05 起

注: SSE 官方按月归档数据（单月 ZIP/CSV），脚本内部按月循环拼接
    (akshare option_finance_* 走东财，2018 前数据缺失；本脚本走交易所补全)

下载约束: 看涨(C) 与 看跌(P) 必须同时下载, 不能分开
  - SSE 每天 CSV 天然包含同一 ETF 当日的所有 call + put 合约
  - 本脚本不按 call_put 过滤, 全部保留在同一份 CSV 中
  - 若需按 call/put 拆分, 请在下游用 df[df.call_put=='认购'] 处理

用法:
  # 单月
  python fetch_sse_option.py --year 2024 --month 1 --product 50ETF
  # 日期范围（内部按月切片）
  python fetch_sse_option.py --start 2023-01-01 --end 2023-12-31 --product 50ETF
  # 全部品种
  python fetch_sse_option.py --year 2024 --month 1
  # 指定保存目录
  python fetch_sse_option.py --year 2024 --month 1 --product 50ETF --save-dir build/data/option_sse

输出:
  {save_dir}/{YYYY-MM}/{product}/{YYYY-MM-DD}.csv  每天一个 CSV
  列: trade_date, product, contract_code, contract_name, call_put,
      strike_price, open, high, low, close, settlement, prev_settlement,
      volume, turnover, open_interest
"""
import argparse
import sys
import zipfile
import io
import time
from datetime import datetime, timedelta
from pathlib import Path
from typing import Iterator

try:
    import requests
    import pandas as pd
except ImportError as e:
    print(f"错误: 缺少依赖 {e.name}: pip install requests pandas")
    sys.exit(1)


# 上交所 ETF 期权配置
SSE_PRODUCTS = {
    "50ETF":      {"name": "上证50ETF期权",     "underlying": "510050",  "first_date": "2015-02-09"},
    "300ETF":     {"name": "沪深300ETF期权",    "underlying": "510300",  "first_date": "2019-12-23"},
    "500ETF":     {"name": "中证500ETF期权",    "underlying": "510500",  "first_date": "2022-09-19"},
    "STAR50ETF":  {"name": "科创50ETF期权",     "underlying": "588000",  "first_date": "2023-06-05"},
}

BASE_URL = "http://yunhq.sse.com.cn:32041/v1/sse1/options"
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    "Referer":    "http://www.sse.com.cn/assortment/options/price/",
}


def fetch_month_zip(product: str, year: int, month: int) -> bytes:
    """
    下载 SSE 某月某品种期权 ZIP 数据
    官方 URL 模板: /v1/sse1/options/{endpoint}/{product}/{YYYYMM}.zip
    """
    yyyymm = f"{year}{month:02d}"
    # 优先尝试 .zip 格式
    for endpoint in ["expiry", "day", "kline"]:
        url = f"{BASE_URL}/{endpoint}/{product}/{yyyymm}.zip"
        try:
            resp = requests.get(url, headers=HEADERS, timeout=30)
            if resp.status_code == 200 and len(resp.content) > 100:
                # 验证 ZIP magic bytes
                if resp.content[:4] == b"PK\x03\x04":
                    return resp.content
        except requests.RequestException:
            continue
        time.sleep(0.5)
    return b""


def extract_zip_csvs(content: bytes) -> dict:
    """
    解压 SSE ZIP 文件
    返回: {filename_in_zip: csv_text}
    """
    if not content:
        return {}
    try:
        z = zipfile.ZipFile(io.BytesIO(content))
        result = {}
        for name in z.namelist():
            if name.endswith(".csv") or name.endswith(".txt"):
                # SSE 内部 CSV 通常是 GBK 编码
                raw = z.read(name)
                for enc in ["gbk", "gb18030", "utf-8"]:
                    try:
                        result[name] = raw.decode(enc)
                        break
                    except UnicodeDecodeError:
                        continue
        return result
    except zipfile.BadZipFile as e:
        print(f"  [WARN] ZIP 解析失败: {e}")
        return {}


def parse_sse_csv(csv_text: str, file_name: str) -> tuple:
    """
    解析 SSE 单日 CSV 文件
    返回: (date_str, DataFrame)
    """
    if not csv_text.strip():
        return None, pd.DataFrame()

    # SSE CSV 通常以 GBK 编码, 以制表符或逗号分隔
    # 尝试多种分隔符
    for sep in ["\t", ","]:
        try:
            df = pd.read_csv(io.StringIO(csv_text), sep=sep, encoding="utf-8")
            if len(df.columns) > 3:
                break
        except Exception:
            continue
    else:
        return None, pd.DataFrame()

    # SSE 文件名通常是 "YYYYMMDD_contract.csv" 格式
    import re
    m = re.search(r"(\d{8})", file_name)
    if m:
        date_str = f"{m.group(1)[:4]}-{m.group(1)[4:6]}-{m.group(1)[6:8]}"
    else:
        date_str = datetime.now().strftime("%Y-%m-%d")

    # 规范化列名（去除空格）
    df.columns = [str(c).strip() for c in df.columns]

    # 映射列
    col_map = {}
    for col in df.columns:
        if "合约代码" in col or col == "CONTRACT_ID":
            col_map[col] = "contract_code"
        elif "合约简称" in col or col == "CONTRACT_NAME":
            col_map[col] = "contract_name"
        elif "合约类型" in col or "看涨" in col:
            col_map[col] = "call_put"
        elif "行权价" in col:
            col_map[col] = "strike_price"
        elif col == "开盘价" or col == "OPEN":
            col_map[col] = "open"
        elif col == "最高价" or col == "HIGH":
            col_map[col] = "high"
        elif col == "最低价" or col == "LOW":
            col_map[col] = "low"
        elif col == "收盘价" or col == "CLOSE":
            col_map[col] = "close"
        elif "结算价" in col:
            col_map[col] = "settlement"
        elif "前结算价" in col:
            col_map[col] = "prev_settlement"
        elif "持仓量" in col:
            col_map[col] = "open_interest"
        elif "成交量" in col:
            col_map[col] = "volume"
        elif "成交额" in col:
            col_map[col] = "turnover"

    df = df.rename(columns=col_map)
    df["trade_date"] = date_str
    return date_str, df


def fetch_one_month(product: str, year: int, month: int, save_dir: Path) -> dict:
    """
    拉取一个月某品种数据, 按日保存 CSV
    返回: {date_str: row_count}
    """
    print(f"  -> {product} {year}-{month:02d}: 下载 ZIP...")
    content = fetch_month_zip(product, year, month)
    if not content:
        print(f"  [WARN] {product} {year}-{month:02d} 无数据（未上市或 ZIP URL 失效）")
        return {}

    csvs = extract_zip_csvs(content)
    if not csvs:
        print(f"  [WARN] {product} {year}-{month:02d} ZIP 内无 CSV")
        return {}

    month_dir = save_dir / f"{year}-{month:02d}" / product
    month_dir.mkdir(parents=True, exist_ok=True)

    result = {}
    for fname, csv_text in csvs.items():
        date_str, df = parse_sse_csv(csv_text, fname)
        if df.empty:
            continue
        df["product"] = product
        out_path = month_dir / f"{date_str}.csv"
        # 列顺序统一
        output_cols = [
            "trade_date", "product", "contract_code", "contract_name", "call_put",
            "strike_price", "open", "high", "low", "close",
            "settlement", "prev_settlement", "volume", "turnover", "open_interest",
        ]
        for col in output_cols:
            if col not in df.columns:
                df[col] = pd.NA
        df[output_cols].to_csv(out_path, index=False, encoding="utf-8-sig")
        result[date_str] = len(df)

    print(f"  [OK] {product} {year}-{month:02d}: {len(result)} 天, 共 {sum(result.values())} 合约-日")
    return result


def monthrange(start: datetime, end: datetime) -> Iterator[tuple]:
    """按月迭代"""
    cur = datetime(start.year, start.month, 1)
    end_m = datetime(end.year, end.month, 1)
    while cur <= end_m:
        yield cur.year, cur.month
        if cur.month == 12:
            cur = datetime(cur.year + 1, 1, 1)
        else:
            cur = datetime(cur.year, cur.month + 1, 1)


def main():
    parser = argparse.ArgumentParser(
        description="从上交所官方获取 ETF 期权日终行情（避开东财）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--year",      type=int, help="年份（配合 --month 使用）")
    parser.add_argument("--month",     type=int, help="月份 1-12")
    parser.add_argument("--start",     type=str, help="起始日期 YYYY-MM-DD")
    parser.add_argument("--end",       type=str, help="结束日期 YYYY-MM-DD")
    parser.add_argument("--product",   type=str, choices=list(SSE_PRODUCTS.keys()) + ["all"], default="all",
                        help="ETF 期权品种，默认 all")
    parser.add_argument("--save-dir",  type=str, default="data/option_sse", help="CSV 保存目录")
    args = parser.parse_args()

    # 解析月份范围
    months = []
    if args.year and args.month:
        months = [(args.year, args.month)]
    elif args.start and args.end:
        start = datetime.strptime(args.start, "%Y-%m-%d")
        end = datetime.strptime(args.end, "%Y-%m-%d")
        months = list(monthrange(start, end))
    else:
        print("错误: 必须指定 (--year --month) 或 (--start --end)")
        sys.exit(1)

    products = list(SSE_PRODUCTS.keys()) if args.product == "all" else [args.product]
    save_dir = Path(args.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    print(f"开始获取 SSE ETF 期权: {len(months)} 个月, 品种: {products}")
    print(f"保存目录: {save_dir.absolute()}")
    print("=" * 70)

    total_days = 0
    for year, month in months:
        for prod in products:
            cfg = SSE_PRODUCTS[prod]
            # 校验该品种是否已上市
            first = datetime.strptime(cfg["first_date"], "%Y-%m-%d")
            cur_month = datetime(year, month, 1)
            if cur_month < first:
                print(f"  [SKIP] {prod} 上市日为 {cfg['first_date']}, 跳过 {year}-{month:02d}")
                continue
            result = fetch_one_month(prod, year, month, save_dir)
            total_days += len(result)
        time.sleep(1)  # 月份间隔, 避免被限频

    print("=" * 70)
    print(f"完成: 累计 {total_days} 天合约数据")


if __name__ == "__main__":
    main()
