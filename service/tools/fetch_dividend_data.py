#!/usr/bin/env python3
"""
使用 BaoStock 查询股票分红/除权/送股等公司行为数据

功能:
  1. 查询指定标的在指定日期的除权除息事件
  2. 输出 JSON 供 C++ 日终管线消费
  3. 可选：下载分红 CSV 供 C++ GetDividendInfo 读取

数据源: BaoStock query_dividend_data()
  - dividend: 除权除息日
  - bonus_amount: 送股比例 (每10股送X股)
  - transfer_amount: 转增比例 (每10股转增X股)
  - divi_cash: 派息金额 (每10股派X元)
  - record_date: 股权登记日

用法:
  # 查询今日是否有除权除息
  python fetch_dividend_data.py "000001.SZ,600519.SH" --check-today

  # 查询指定日期
  python fetch_dividend_data.py "000001.SZ,600519.SH" --date 2026-07-25

  # 下载分红 CSV 到 data/dividend/ 目录 (供 C++ 读取)
  python fetch_dividend_data.py "000001.SZ,600519.SH" --download --data-dir build/data

  # 输出 JSON (供管线消费)
  python fetch_dividend_data.py "000001.SZ,600519.SH" --check-today --json
"""
import argparse
import json
import os
import sys
from datetime import datetime, date

try:
    import baostock as bs
    import pandas as pd
except ImportError:
    print("错误: 请先安装 baostock 和 pandas:")
    print("  pip install baostock pandas")
    sys.exit(1)


# ============================================================
# 代码格式转换 (复用 download_etf_bs.py 的逻辑)
# ============================================================

def convert_to_baostock_code(symbol: str) -> str:
    """
    将多种格式转为 baostock 格式 (sh.600519 / sz.000001)
    """
    sym = symbol.strip().upper()
    if "." in sym:
        parts = sym.split(".")
        code, market = parts[0], parts[1]
        if market in ("SH", "SSE"):
            return f"sh.{code}"
        elif market in ("SZ", "SZSE"):
            return f"sz.{code}"
        return f"{market.lower()}.{code}"

    code = sym
    if code.startswith(("6", "9")):
        return f"sh.{code}"
    elif code.startswith(("0", "3", "2")):
        return f"sz.{code}"
    return f"sh.{code}"


def to_code_exchange(symbol: str) -> str:
    """sh.600519 -> 600519.SH"""
    sym = symbol.strip()
    if "." not in sym:
        return sym
    parts = sym.split(".")
    left, right = parts[0], parts[1].upper()
    if left.isdigit():
        return sym.upper()
    exchange_map = {"sh": "SH", "sse": "SH", "sz": "SZ", "szse": "SZ", "bj": "BJ"}
    exchange = exchange_map.get(left.lower(), right)
    return f"{parts[0]}.{exchange}"


# ============================================================
# 分红数据查询
# ============================================================

def query_dividend(bs_code: str, year: int) -> pd.DataFrame:
    """
    查询某标的某年的分红除权数据

    BaoStock 返回字段:
      code, companyCode, code_name, reportDate, year,
      dividend(除权除息日), dividend_type, record_date(股权登记日),
      ex_dividend_price, bonus_amount(送股), transfer_amount(转增),
      divi_cash(派息), allot_amount(配股), allot_price(配股价)
    """
    try:
        rs = bs.query_dividend_data(bs_code, year)
        if rs.error_code != '0':
            print(f"  [{bs_code}] {year}年分红查询失败: {rs.error_msg}", file=sys.stderr)
            return pd.DataFrame()

        rows = []
        while rs.next():
            rows.append(rs.get_row_data())

        if not rows:
            return pd.DataFrame()

        df = pd.DataFrame(rows, columns=rs.fields)
        return df

    except Exception as e:
        print(f"  [{bs_code}] {year}年分红查询异常: {e}", file=sys.stderr)
        return pd.DataFrame()


def check_ex_dividend(symbols: list, check_date: str) -> list:
    """
    检查指定标的在指定日期是否有除权除息

    Args:
        symbols: 标的列表 (CODE.EXCHANGE 格式)
        check_date: 检查日期 YYYY-MM-DD

    Returns:
        list of dict: 有除权除息事件的标的信息
    """
    target_date = check_date  # YYYY-MM-DD
    target_year = int(check_date[:4])

    # 需要查两年: 当年和上一年 (年报分红通常在次年实施)
    years = [target_year, target_year - 1]

    results = []

    for symbol in symbols:
        bs_code = convert_to_baostock_code(symbol)
        display_code = to_code_exchange(bs_code)

        for year in years:
            df = query_dividend(bs_code, year)
            if df.empty:
                continue

            # 过滤: 除权除息日 == check_date
            if 'dividend' not in df.columns:
                continue

            # dividend 字段格式可能是 YYYY-MM-DD 或 YYYYMMDD
            for _, row in df.iterrows():
                ex_date = str(row.get('dividend', '')).strip()
                # 标准化日期格式
                if len(ex_date) == 8 and ex_date.isdigit():
                    ex_date = f"{ex_date[:4]}-{ex_date[4:6]}-{ex_date[6:8]}"

                if ex_date == target_date:
                    # 解析数据
                    bonus = float(row.get('bonus_amount', 0) or 0)       # 送股 (每10股)
                    transfer = float(row.get('transfer_amount', 0) or 0)  # 转增 (每10股)
                    cash = float(row.get('divi_cash', 0) or 0)           # 派息 (每10股)
                    record_date = str(row.get('record_date', '')).strip()
                    if len(record_date) == 8 and record_date.isdigit():
                        record_date = f"{record_date[:4]}-{record_date[4:6]}-{record_date[6:8]}"

                    # 判断类型
                    actions = []
                    if cash > 0:
                        actions.append(f"派息{cash}元/10股")
                    if bonus > 0:
                        actions.append(f"送股{bonus}股/10股")
                    if transfer > 0:
                        actions.append(f"转增{transfer}股/10股")

                    results.append({
                        "symbol": display_code,
                        "bs_code": bs_code,
                        "name": row.get('code_name', ''),
                        "ex_dividend_date": ex_date,
                        "record_date": record_date,
                        "bonus_per_10": bonus,        # 每10股送股数
                        "transfer_per_10": transfer,   # 每10股转增数
                        "cash_per_10": cash,           # 每10股派息(元)
                        "actions": actions,
                        "action_type": "dividend" if cash > 0 and bonus == 0 and transfer == 0
                                       else "split" if cash == 0 and (bonus > 0 or transfer > 0)
                                       else "mixed",
                        "year": year,
                    })

    return results


# ============================================================
# action_type 编码
# ============================================================

def compute_action_type(bonus: float, transfer: float, cash: float, allot: float) -> int:
    """
    公司行为类型编码 (TINYINT)
    0=未知, 1=分红, 2=送股, 3=转增, 4=送转, 5=分红送转, 6=配股, 7=混合
    """
    has_cash = cash > 0
    has_bonus = bonus > 0
    has_transfer = transfer > 0
    has_allot = allot > 0
    has_stock = has_bonus or has_transfer

    if has_allot and (has_cash or has_stock):
        return 7  # 混合（含配股）
    if has_allot:
        return 6  # 配股
    if has_cash and has_stock:
        return 5  # 分红送转
    if has_bonus and has_transfer:
        return 4  # 送转
    if has_cash:
        return 1  # 分红
    if has_bonus:
        return 2  # 送股
    if has_transfer:
        return 3  # 转增
    return 0  # 未知


def normalize_date(val: str) -> str:
    """标准化日期: YYYYMMDD -> YYYY-MM-DD, 空值返回空字符串"""
    s = str(val).strip()
    if len(s) == 8 and s.isdigit():
        return f"{s[:4]}-{s[4:6]}-{s[6:8]}"
    if len(s) >= 10:
        return s[:10]
    return ""


# ============================================================
# 分红 CSV 下载 (供 C++ FinanceDB 导入 DuckDB)
# ============================================================

def download_dividend_csv(symbols: list, data_dir: str, years: list = None):
    """
    下载分红数据并保存为 DuckDB 兼容 CSV 格式

    输出: {data_dir}/dividend/{symbol}_dividend.csv
    列: symbol, announce_date, report_year, ex_dividend_date, record_date,
        implement_date, bonus_per_10, transfer_per_10, cash_per_10,
        allot_per_10, allot_price, ex_div_price, action_type
    """
    if years is None:
        current_year = date.today().year
        years = list(range(current_year - 5, current_year + 1))

    dividend_dir = os.path.join(data_dir, "dividend")
    os.makedirs(dividend_dir, exist_ok=True)

    for symbol in symbols:
        bs_code = convert_to_baostock_code(symbol)
        display_code = to_code_exchange(bs_code)

        all_rows = []
        for year in years:
            df = query_dividend(bs_code, year)
            if df.empty:
                continue
            for _, row in df.iterrows():
                all_rows.append((year, row))

        if not all_rows:
            print(f"  [{display_code}] 无分红数据")
            continue

        # 去重 (同一除权日可能因查两年而重复)
        seen_dates = set()
        unique_rows = []
        for year, row in all_rows:
            ex_date = normalize_date(row.get('dividend', ''))
            if ex_date and ex_date not in seen_dates:
                seen_dates.add(ex_date)
                unique_rows.append((year, row))

        # 按除权除息日排序
        unique_rows.sort(key=lambda r: r[1].get('dividend', ''))

        # 写入 CSV (DuckDB 兼容格式)
        csv_path = os.path.join(dividend_dir, f"{display_code}_dividend.csv")
        with open(csv_path, "w", encoding="utf-8") as f:
            f.write("symbol,announce_date,report_year,ex_dividend_date,record_date,"
                    "implement_date,bonus_per_10,transfer_per_10,cash_per_10,"
                    "allot_per_10,allot_price,ex_div_price,action_type\n")
            for year, row in unique_rows:
                bonus = float(row.get('bonus_amount', 0) or 0)
                transfer = float(row.get('transfer_amount', 0) or 0)
                cash = float(row.get('divi_cash', 0) or 0)
                allot = float(row.get('allot_amount', 0) or 0)
                allot_price = float(row.get('allot_price', 0) or 0)
                ex_div_price = row.get('ex_dividend_price', '') or ''
                announce = normalize_date(row.get('reportDate', ''))
                record = normalize_date(row.get('record_date', ''))
                ex_div = normalize_date(row.get('dividend', ''))
                # implement_date: BaoStock 无直接字段，用除权除息日近似
                implement = ex_div
                action_type = compute_action_type(bonus, transfer, cash, allot)

                f.write(f"{display_code},{announce},{year},{ex_div},{record},"
                        f"{implement},{bonus},{transfer},{cash},"
                        f"{allot},{allot_price},{ex_div_price},{action_type}\n")

        print(f"  [{display_code}] 分红数据: {csv_path} ({len(unique_rows)} 条)")


# ============================================================
# 主流程
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description="BaoStock 分红除权查询",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("symbols", help="标的列表，逗号分隔 (如 000001.SZ,600519.SH)")
    parser.add_argument("--check-today", action="store_true",
                        help="检查今日是否有除权除息")
    parser.add_argument("--date", default=None,
                        help="检查日期 YYYY-MM-DD (默认今天)")
    parser.add_argument("--download", action="store_true",
                        help="下载分红 CSV 到 data/dividend/ 目录")
    parser.add_argument("--data-dir", default="build/data",
                        help="数据目录 (默认 build/data)")
    parser.add_argument("--json", action="store_true",
                        help="输出 JSON 格式 (供管线消费)")
    parser.add_argument("--years", default=None,
                        help="下载年份范围，如 2020-2026")

    args = parser.parse_args()
    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]

    check_date = args.date or date.today().strftime("%Y-%m-%d")

    # 登录 BaoStock
    print("登录 BaoStock...", file=sys.stderr)
    lg = bs.login()
    if lg.error_code != '0':
        print(f"登录失败: {lg.error_msg}", file=sys.stderr)
        sys.exit(1)

    try:
        # 1. 检查除权除息
        if args.check_today or args.date:
            results = check_ex_dividend(symbols, check_date)

            if args.json:
                output = {
                    "date": check_date,
                    "has_corporate_action": len(results) > 0,
                    "affected_symbols": results,
                    "count": len(results),
                }
                print(json.dumps(output, ensure_ascii=False, indent=2))
            else:
                if results:
                    print(f"\n⚠️  {check_date} 有 {len(results)} 只标的除权除息:")
                    print("-" * 60)
                    for item in results:
                        actions_str = ", ".join(item["actions"])
                        print(f"  {item['symbol']} {item['name']}")
                        print(f"    除权除息日: {item['ex_dividend_date']}")
                        print(f"    登记日: {item['record_date']}")
                        print(f"    操作: {actions_str}")
                        print(f"    类型: {item['action_type']}")
                        print()
                else:
                    print(f"\n✅ {check_date} 无除权除息事件")

        # 2. 下载分红 CSV
        if args.download:
            years = None
            if args.years:
                parts = args.years.split("-")
                years = list(range(int(parts[0]), int(parts[1]) + 1))

            print(f"\n下载分红数据到 {args.data_dir}/dividend/ ...")
            download_dividend_csv(symbols, args.data_dir, years)

    finally:
        bs.logout()


if __name__ == "__main__":
    main()
