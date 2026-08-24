#!/usr/bin/env python3
"""
从中金所（CFFEX）官方获取股指期权日终行情数据

数据源: 中国金融期货交易所 http://www.cffex.com.cn/
品种: 沪深300股指期权(IO), 上证50股指期权(HO), 中证1000股指期权(MO)

覆盖范围: 各品种上市日 ~ 至今
  - IO: 2019-12-23 起
  - HO: 2020-04-16 起
  - MO: 2022-07-22 起

特点: 每个交易日 15:30 后发布当日 XLS 文件，含 OHLCV + Greeks + 隐含波动率
      (akshare option_finance_* 走东财，2019/2020 数据缺失；本脚本走交易所补全)

下载约束: 看涨(C) 与 看跌(P) 必须同时下载, 不能分开
  - CFFEX 每个 XLS 文件天然包含同一品种当日的所有 call + put 合约
  - 本脚本不按 call_put 过滤, 全部保留在同一份 CSV 中
  - 若需按 call/put 拆分, 请在下游读取 CSV 后用 df[df.call_put=='认购'] 处理

用法:
  # 单日
  python fetch_cffex_option.py --date 2024-01-15
  # 日期范围
  python fetch_cffex_option.py --start 2024-01-15 --end 2024-01-31
  # 单一品种
  python fetch_cffex_option.py --start 2024-01-15 --end 2024-01-31 --product IO
  # 指定保存目录
  python fetch_cffex_option.py --date 2024-01-15 --save-dir build/data/option_cffex

输出:
  {save_dir}/{YYYY-MM-DD}_{product}.csv  每行一个合约
  列: trade_date, product, contract_code, contract_name, call_put,
      open, high, low, close, settlement, prev_settlement,
      volume, turnover, open_interest, exercise_volume,
      delta, gamma, vega, theta, implied_volatility
"""
import argparse
import sys
import re
import time
from datetime import datetime, timedelta
from pathlib import Path

try:
    import requests
    import pandas as pd
    from bs4 import BeautifulSoup
except ImportError as e:
    print(f"错误: 缺少依赖 {e.name}: pip install requests beautifulsoup4 pandas")
    sys.exit(1)


# CFFEX 股指期权品种配置
CFFEX_PRODUCTS = {
    "IO":  "沪深300股指期权",
    "HO":  "上证50股指期权",
    "MO":  "中证1000股指期权",
}

BASE_URL = "http://www.cffex.com.cn/sj/hqsj/option"
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36",
    "Referer":    "http://www.cffex.com.cn/",
}


def fetch_daily_index(date_str: str) -> str:
    """
    获取 CFFEX 当日期权日终数据索引页 HTML
    返回: HTML 文本，包含 IO/HO/MO 三个 XLS 链接
    """
    url = f"{BASE_URL}/{date_str}/index.html"
    try:
        resp = requests.get(url, headers=HEADERS, timeout=20)
        if resp.status_code != 200:
            return ""
        resp.encoding = "utf-8"
        return resp.text
    except requests.RequestException as e:
        print(f"  [WARN] {date_str} 索引页请求失败: {e}")
        return ""


def download_excel(url: str, max_retries: int = 2) -> bytes:
    """下载 XLS 文件二进制"""
    for attempt in range(max_retries + 1):
        try:
            resp = requests.get(url, headers=HEADERS, timeout=30)
            if resp.status_code == 200 and len(resp.content) > 1000:
                return resp.content
        except requests.RequestException as e:
            if attempt == max_retries:
                print(f"  [WARN] 下载失败 {url}: {e}")
        time.sleep(1)
    return b""


def parse_cffex_excel(content: bytes, date_str: str, product_hint: str = None) -> pd.DataFrame:
    """
    解析 CFFEX 期权日终 XLS 文件

    典型表格结构:
      行1-5: 标题与表头
      行6+:  数据（合约代码, 合约名称, 期权类型, 行权价, OHLC, 结算价, ...)
    """
    import io
    try:
        # CFFEX 旧文件用 xls, 新文件用 xlsx
        try:
            df_raw = pd.read_excel(io.BytesIO(content), engine="openpyxl", header=None)
        except Exception:
            df_raw = pd.read_excel(io.BytesIO(content), engine="xlrd", header=None)
    except Exception as e:
        print(f"  [WARN] {date_str} 解析失败: {e}")
        return pd.DataFrame()

    if df_raw.empty:
        return pd.DataFrame()

    # 找到合约代码列起始行（含 '合约代码' 或 '合约' 字样的行）
    header_row = None
    for i, row in df_raw.iterrows():
        row_str = " ".join(str(v) for v in row.values if pd.notna(v))
        if "合约代码" in row_str or ("合约" in row_str and "行权价" in row_str):
            header_row = i
            break

    if header_row is None:
        print(f"  [WARN] {date_str} 未找到表头")
        return pd.DataFrame()

    # 用该行作为列名
    df = pd.read_excel(io.BytesIO(content), header=header_row)

    # 清理: 去掉全空行
    df = df.dropna(how="all").reset_index(drop=True)

    # 推断品种代码（列名含 'IO'/'HO'/'MO' 或通过文件名）
    product_code = product_hint or ""
    if not product_code:
        if "沪深300" in str(df.columns):
            product_code = "IO"
        elif "上证50" in str(df.columns):
            product_code = "HO"
        elif "中证1000" in str(df.columns):
            product_code = "MO"

    # 规范化列名
    col_map = {}
    for col in df.columns:
        col_str = str(col).strip()
        if "合约代码" in col_str or col_str == "contractId":
            col_map[col] = "contract_code"
        elif "合约名称" in col_str or col_str == "contractName":
            col_map[col] = "contract_name"
        elif col_str in ("看涨/看跌", "期权类型", "call_put"):
            col_map[col] = "call_put"
        elif "行权价" in col_str:
            col_map[col] = "strike_price"
        elif col_str == "开盘价":
            col_map[col] = "open"
        elif col_str == "最高价":
            col_map[col] = "high"
        elif col_str == "最低价":
            col_map[col] = "low"
        elif col_str == "收盘价":
            col_map[col] = "close"
        elif "结算价" in col_str:
            col_map[col] = "settlement"
        elif "前结算价" in col_str or col_str == "昨结算":
            col_map[col] = "prev_settlement"
        elif col_str == "成交量":
            col_map[col] = "volume"
        elif col_str == "成交额":
            col_map[col] = "turnover"
        elif "持仓量" in col_str or col_str == "持仓":
            col_map[col] = "open_interest"
        elif "行权量" in col_str:
            col_map[col] = "exercise_volume"
        elif "Delta" in col_str:
            col_map[col] = "delta"
        elif "Gamma" in col_str:
            col_map[col] = "gamma"
        elif "Vega" in col_str:
            col_map[col] = "vega"
        elif "Theta" in col_str:
            col_map[col] = "theta"
        elif "隐含波动率" in col_str:
            col_map[col] = "implied_volatility"

    df = df.rename(columns=col_map)

    # 添加日期和品种列
    df["trade_date"] = date_str
    df["product"] = product_code

    # 统一输出列
    output_cols = [
        "trade_date", "product", "contract_code", "contract_name", "call_put",
        "strike_price", "open", "high", "low", "close", "settlement",
        "prev_settlement", "volume", "turnover", "open_interest", "exercise_volume",
        "delta", "gamma", "vega", "theta", "implied_volatility",
    ]
    for col in output_cols:
        if col not in df.columns:
            df[col] = pd.NA
    return df[output_cols]


def fetch_one_day(date_str: str, products: list, save_dir: Path) -> dict:
    """
    拉取一个交易日的所有品种期权数据并保存
    返回: {product: csv_path or None}
    """
    result = {}
    html = fetch_daily_index(date_str)
    if not html:
        return result

    soup = BeautifulSoup(html, "html.parser")
    rows = []

    # 找到所有 XLS 链接，按品种代码分类
    for a in soup.find_all("a", href=True):
        href = a["href"]
        text = a.get_text(strip=True)
        if not re.search(r"\.(xls|xlsx)$", href, re.IGNORECASE):
            continue

        # 根据 text 推断品种
        prod = None
        for code, name in CFFEX_PRODUCTS.items():
            if name in text or code in text:
                prod = code
                break
        if prod and prod not in products:
            continue

        # 拼绝对 URL
        if not href.startswith("http"):
            href = f"http://www.cffex.com.cn{href}" if href.startswith("/") else f"{BASE_URL}/{date_str}/{href}"

        content = download_excel(href)
        if not content:
            continue

        df = parse_cffex_excel(content, date_str, product_hint=prod)
        if df.empty:
            continue

        rows.append(df)

        # 单品种保存
        out_path = save_dir / f"{date_str}_{prod}.csv"
        df.to_csv(out_path, index=False, encoding="utf-8-sig")
        result[prod] = str(out_path)
        print(f"  [OK] {date_str} {prod}: {len(df)} 合约 -> {out_path.name}")

    # 合并版（all in one）
    if rows:
        merged = pd.concat(rows, ignore_index=True)
        merged_path = save_dir / f"{date_str}.csv"
        merged.to_csv(merged_path, index=False, encoding="utf-8-sig")
        result["_all"] = str(merged_path)

    return result


def daterange(start: datetime, end: datetime):
    cur = start
    while cur <= end:
        yield cur
        cur += timedelta(days=1)


def main():
    parser = argparse.ArgumentParser(
        description="从中金所官方获取股指期权日终行情（避开东财）",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python fetch_cffex_option.py --date 2024-01-15
  python fetch_cffex_option.py --start 2024-01-15 --end 2024-01-31
  python fetch_cffex_option.py --start 2024-01-15 --end 2024-01-31 --product IO
        """,
    )
    parser.add_argument("--date",       type=str, help="单日 YYYY-MM-DD")
    parser.add_argument("--start",      type=str, help="起始日期 YYYY-MM-DD")
    parser.add_argument("--end",        type=str, help="结束日期 YYYY-MM-DD")
    parser.add_argument("--product",    type=str, choices=list(CFFEX_PRODUCTS.keys()) + ["all"], default="all",
                        help="品种过滤 (IO/HO/MO), 默认 all")
    parser.add_argument("--save-dir",   type=str, default="data/option_cffex", help="CSV 保存目录")
    parser.add_argument("--skip-weekend", action="store_true", default=True, help="跳过周末（默认开）")
    args = parser.parse_args()

    # 参数校验
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

    products = list(CFFEX_PRODUCTS.keys()) if args.product == "all" else [args.product]

    print(f"开始获取 CFFEX 期权数据: {start.date()} ~ {end.date()}, 品种: {products}")
    print(f"保存目录: {save_dir.absolute()}")
    print("=" * 70)

    success, fail, skipped = 0, 0, 0
    for d in daterange(start, end):
        date_str = d.strftime("%Y-%m-%d")

        # 跳过周末（默认行为）
        if args.skip_weekend and d.weekday() >= 5:
            skipped += 1
            continue

        print(f"[{date_str}] ({d.strftime('%A')})")
        result = fetch_one_day(date_str, products, save_dir)
        if result:
            success += 1
        else:
            fail += 1
            print(f"  [WARN] {date_str} 无可用数据（节假日或未发布）")

    print("=" * 70)
    print(f"完成: 成功 {success} 天, 失败/无数据 {fail} 天, 跳过 {skipped} 天（周末）")


if __name__ == "__main__":
    main()
