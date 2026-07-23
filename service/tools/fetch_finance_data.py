"""
使用 BaoStock 获取季频财务数据

数据源: BaoStock (http://baostock.com)
支持: 盈利能力、营运能力、成长能力、偿债能力、现金流量、杜邦分析

用法:
  # 下载单只股票全部财务数据（默认近5年）
  python fetch_finance_data.py --code sh.600519 --download

  # 下载指定类别
  python fetch_finance_data.py --code sh.600519 --download --category profit

  # 指定年份范围
  python fetch_finance_data.py --code sh.600519 --download --start 2020 --end 2025

  # 查询已缓存数据（JSON 输出，供 C++ Handler / Agent Tool）
  python fetch_finance_data.py --code sh.600519 --category profit --json

  # 列出支持的财务类别
  python fetch_finance_data.py --list
"""
import argparse
import json
import os
import sys
import time
from datetime import datetime

try:
    import baostock as bs
    import pandas as pd
except ImportError:
    print("错误: 请先安装 baostock 和 pandas:")
    print("  pip install baostock pandas")
    sys.exit(1)


# ============================================================
# 财务类别定义
# ============================================================

CATEGORIES = {
    "profit": {
        "name": "盈利能力",
        "api": "query_profit_data",
        "fields": [
            "code", "pubDate", "statDate",
            "roeAvg", "npMargin", "gpMargin",
            "netProfit", "epsTTM", "MBRevenue",
            "totalShare", "liqaShare",
        ],
        "display_cols": [
            ("statDate", "报告期"),
            ("roeAvg", "ROE(%)"),
            ("npMargin", "净利率(%)"),
            ("gpMargin", "毛利率(%)"),
            ("netProfit", "净利润(元)"),
            ("epsTTM", "EPS(TTM)"),
        ],
    },
    "operation": {
        "name": "营运能力",
        "api": "query_operation_data",
        "fields": [
            "code", "pubDate", "statDate",
            "NRTurnRatio", "NRTurnDays",
            "INVTurnRatio", "INVTurnDays",
            "CATurnRatio", "ASTurnRatio",
        ],
        "display_cols": [
            ("statDate", "报告期"),
            ("NRTurnRatio", "应收周转率"),
            ("NRTurnDays", "应收周转天数"),
            ("INVTurnRatio", "存货周转率"),
            ("INVTurnDays", "存货周转天数"),
            ("ASTurnRatio", "总资产周转率"),
        ],
    },
    "growth": {
        "name": "成长能力",
        "api": "query_growth_data",
        "fields": [
            "code", "pubDate", "statDate",
            "YOYEquity", "YOYAsset",
            "YOYNI", "YOYEPSBasic", "YOYPNI",
        ],
        "display_cols": [
            ("statDate", "报告期"),
            ("YOYNI", "净利润同比(%)"),
            ("YOYEPSBasic", "基本EPS同比(%)"),
            ("YOYPNI", "归母净利润同比(%)"),
            ("YOYEquity", "净资产同比(%)"),
            ("YOYAsset", "总资产同比(%)"),
        ],
    },
    "balance": {
        "name": "偿债能力",
        "api": "query_balance_data",
        "fields": [
            "code", "pubDate", "statDate",
            "currentRatio", "quickRatio",
            "cashRatio", "debtToAsset",
        ],
        "display_cols": [
            ("statDate", "报告期"),
            ("currentRatio", "流动比率"),
            ("quickRatio", "速动比率"),
            ("cashRatio", "现金比率"),
            ("debtToAsset", "资产负债率(%)"),
        ],
    },
    "cashflow": {
        "name": "现金流量",
        "api": "query_cash_flow_data",
        "fields": [
            "code", "pubDate", "statDate",
            "netProfit", "salesService", "taxSurcharge",
            "cashPayAcquire", "netCashFlowAct",
            "netCashFlowInv", "netCashFlowFin",
        ],
        "display_cols": [
            ("statDate", "报告期"),
            ("netCashFlowAct", "经营现金流净额"),
            ("netCashFlowInv", "投资现金流净额"),
            ("netCashFlowFin", "筹资现金流净额"),
            ("salesService", "销售商品收到现金"),
            ("cashPayAcquire", "购买商品支付现金"),
        ],
    },
    "dupont": {
        "name": "杜邦分析",
        "api": "query_dupont_data",
        "fields": [
            "code", "pubDate", "statDate",
            "dupontROE", "dupontNetProfit",
            "dupontAssetTurn", "dupontEquityMultiplier",
        ],
        "display_cols": [
            ("statDate", "报告期"),
            ("dupontROE", "ROE(%)"),
            ("dupontNetProfit", "净利率(%)"),
            ("dupontAssetTurn", "总资产周转率"),
            ("dupontEquityMultiplier", "权益乘数"),
        ],
    },
}


# ============================================================
# 代码格式转换
# ============================================================

def convert_to_baostock_code(symbol: str) -> str:
    """
    将多种格式转为 baostock 格式 (sh.600519 / sz.000858)

    支持输入:
      "600519.SH" -> "sh.600519"
      "sh.600519" -> "sh.600519"
      "600519"    -> "sh.600519" (按前缀推断)
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
    elif code.startswith(("0", "2", "3")):
        return f"sz.{code}"
    else:
        return f"sh.{code}"


def to_display_code(bs_code: str) -> str:
    """sh.600519 -> 600519.SH"""
    if not bs_code or "." not in str(bs_code):
        return str(bs_code) if bs_code else bs_code
    parts = str(bs_code).split(".")
    if len(parts) != 2:
        return str(bs_code)
    left, right = parts
    # 已经是 600519.SH 格式
    if left.isdigit():
        return str(bs_code).upper()
    # baostock 格式: sh.600519 -> 600519.SH
    exchange_map = {"sh": "SH", "sz": "SZ", "bj": "BJ"}
    exchange = exchange_map.get(left.lower(), right.upper())
    return f"{right}.{exchange}"


# ============================================================
# 下载逻辑
# ============================================================

def download_category(code: str, category: str, data_dir: str,
                      start_year: int = None, end_year: int = None) -> pd.DataFrame:
    """
    下载某个类别的全部季度数据

    baostock 财务接口每次只返回一个 (year, quarter) 的数据，
    需要遍历所有季度再合并。
    """
    cat_info = CATEGORIES[category]
    api_func = getattr(bs, cat_info["api"])
    all_rows = []

    current_year = datetime.now().year
    start_y = start_year or (current_year - 5)
    end_y = end_year or current_year

    for year in range(start_y, end_y + 1):
        for quarter in range(1, 5):
            try:
                rs = api_func(code=code, year=year, quarter=quarter)
                if rs.error_code != '0':
                    print(f"  [{category}] {year}Q{quarter} 查询失败: {rs.error_msg}")
                    continue
                df = rs.get_data()
                if not df.empty:
                    all_rows.append(df)
            except Exception as e:
                print(f"  [{category}] {year}Q{quarter} 异常: {e}")
            time.sleep(0.3)

    if not all_rows:
        print(f"  [{category}] 无数据")
        return pd.DataFrame()

    result = pd.concat(all_rows, ignore_index=True)
    result = result.drop_duplicates(subset=["statDate"]).sort_values("statDate").reset_index(drop=True)

    # 保存 CSV（文件名使用显示格式 600519.SH.csv）
    out_dir = os.path.join(data_dir, category)
    os.makedirs(out_dir, exist_ok=True)
    display_name = to_display_code(code)
    filepath = os.path.join(out_dir, f"{display_name}.csv")
    result.to_csv(filepath, index=False)
    print(f"  [{category}] 已保存 {filepath} ({len(result)} 条)")
    return result


def download_all(code: str, data_dir: str, categories: list,
                 start_year: int = None, end_year: int = None):
    """下载指定类别（或全部）的财务数据"""
    display_code = to_display_code(code)
    print(f"\n{'=' * 60}")
    print(f"财务数据下载 (BaoStock)")
    print(f"{'=' * 60}")
    print(f"标的: {display_code}")
    print(f"类别: {', '.join(categories)}")
    print(f"年份: {start_year or '近5年'} ~ {end_year or datetime.now().year}")
    print(f"目录: {data_dir}")
    print(f"{'=' * 60}")

    ok_count = 0
    for cat in categories:
        print(f"\n下载 {CATEGORIES[cat]['name']}...")
        df = download_category(code, cat, data_dir, start_year, end_year)
        if not df.empty:
            ok_count += 1

    print(f"\n完成: {ok_count}/{len(categories)} 个类别成功")


# ============================================================
# 查询输出
# ============================================================

def query_json(code: str, category: str, data_dir: str,
               start: str = None, end: str = None) -> dict:
    """读取已缓存的 CSV，JSON 输出"""
    display_name = to_display_code(code)
    filepath = os.path.join(data_dir, category, f"{display_name}.csv")
    if not os.path.exists(filepath):
        return {
            "error": f"无缓存数据，请先下载: --code {code} --download --category {category}",
            "data": [],
        }

    df = pd.read_csv(filepath)

    if start:
        df = df[df["statDate"] >= start]
    if end:
        df = df[df["statDate"] <= end]

    # 数值列转换
    numeric_cols = [c for c in df.columns if c not in ("code", "pubDate", "statDate")]
    for col in numeric_cols:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    # 将 code 列从 baostock 格式转为显示格式
    if "code" in df.columns:
        df["code"] = df["code"].apply(lambda x: to_display_code(x) if x and str(x) != "nan" else None)

    # NaN -> None（JSON 不支持 NaN），逐行清理
    records = []
    for _, row in df.iterrows():
        record = {}
        for k, v in row.items():
            if v is None or (isinstance(v, float) and (v != v)):  # None or NaN
                record[k] = None
            else:
                record[k] = v
        records.append(record)

    mtime = os.path.getmtime(filepath)
    return {
        "code": display_name,
        "category": category,
        "name": CATEGORIES[category]["name"],
        "updated_at": datetime.fromtimestamp(mtime).strftime("%Y-%m-%d %H:%M:%S"),
        "count": len(records),
        "data": records,
    }


def query_print(code: str, category: str, data_dir: str,
                start: str = None, end: str = None):
    """读取已缓存的 CSV，表格打印"""
    display_name = to_display_code(code)
    filepath = os.path.join(data_dir, category, f"{display_name}.csv")
    if not os.path.exists(filepath):
        print(f"无缓存数据: {filepath}")
        print(f"请先运行: python fetch_finance_data.py --code {code} --download --category {category}")
        return

    df = pd.read_csv(filepath)

    if start:
        df = df[df["statDate"] >= start]
    if end:
        df = df[df["statDate"] <= end]

    # 数值列转换
    numeric_cols = [c for c in df.columns if c not in ("code", "pubDate", "statDate")]
    for col in numeric_cols:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    cat_info = CATEGORIES[category]
    display_code = to_display_code(code)

    print(f"\n{'=' * 80}")
    print(f"{display_code} - {cat_info['name']}")
    print(f"{'=' * 80}")
    print(f"数据条数: {len(df)}")
    print(f"{'-' * 80}")

    # 表头
    display_cols = cat_info["display_cols"]
    headers = [label for _, label in display_cols]
    print("  ".join(f"{h:>16}" for h in headers))
    print(f"{'-' * 80}")

    # 数据行（最多显示最近 12 条）
    display_df = df.tail(12)
    for _, row in display_df.iterrows():
        values = []
        for col, _ in display_cols:
            val = row.get(col, "")
            try:
                fval = float(val)
                if abs(fval) >= 1e8:
                    values.append(f"{fval / 1e8:>16.2f}亿")
                elif abs(fval) >= 1e4:
                    values.append(f"{fval / 1e4:>16.2f}万")
                else:
                    values.append(f"{fval:>16.4f}")
            except (ValueError, TypeError):
                values.append(f"{str(val):>16}")
        print("  ".join(values))

    print(f"{'=' * 80}")

    # 统计摘要
    if len(df) > 0 and len(numeric_cols) > 0:
        print(f"\n统计摘要 (最近 {len(df)} 个季度):")
        for col in numeric_cols[:4]:
            series = pd.to_numeric(df[col], errors="coerce").dropna()
            if len(series) > 0:
                label = next((l for c, l in display_cols if c == col), col)
                print(f"  {label}: 最新={series.iloc[-1]:.4f}, "
                      f"均值={series.mean():.4f}, "
                      f"最小={series.min():.4f}, 最大={series.max():.4f}")


# ============================================================
# 列出支持的类别
# ============================================================

def list_categories():
    print(f"\n{'=' * 60}")
    print("支持的财务数据类别")
    print(f"{'=' * 60}")
    for key, info in CATEGORIES.items():
        fields = [f for f in info["fields"] if f not in ("code", "pubDate", "statDate")]
        print(f"\n  --category {key:<12} {info['name']}")
        print(f"  {'':<14} 字段: {', '.join(fields)}")
    print(f"\n{'=' * 60}")
    print("用法:")
    print("  下载: python fetch_finance_data.py --code sh.600519 --download")
    print("  查询: python fetch_finance_data.py --code sh.600519 --category profit --json")
    print(f"{'=' * 60}\n")


# ============================================================
# 主流程
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description="使用 BaoStock 获取季频财务数据",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python fetch_finance_data.py --list
  python fetch_finance_data.py --code sh.600519 --download
  python fetch_finance_data.py --code sh.600519 --download --category profit --start 2022
  python fetch_finance_data.py --code sh.600519 --category profit --json
  python fetch_finance_data.py --code sh.600519 --category profit --start 2024-Q1 --json
        """,
    )
    parser.add_argument("--code", type=str, help="股票代码 (sh.600519 / 600519.SH / 600519)")
    parser.add_argument("--download", action="store_true", help="下载并缓存到本地")
    parser.add_argument("--category", type=str, default=None,
                        help="财务类别 (profit/operation/growth/balance/cashflow/dupont/all，默认 all)")
    parser.add_argument("--start", type=str, help="起始年份或季度 (如 2022 或 2024-Q1)")
    parser.add_argument("--end", type=str, help="结束年份或季度 (如 2025 或 2025-Q2)")
    parser.add_argument("--json", action="store_true", help="JSON 输出（供 C++ Handler / Agent Tool）")
    parser.add_argument("--list", action="store_true", help="列出支持的财务类别")
    parser.add_argument("--data-dir", type=str, default="data/finance", help="缓存目录 (默认 data/finance)")

    args = parser.parse_args()

    if args.list:
        list_categories()
        return

    if not args.code:
        print("错误: 请指定 --code 参数，或使用 --list 查看支持的类别")
        parser.print_help()
        return

    code = convert_to_baostock_code(args.code)

    # 解析类别
    if args.category and args.category != "all":
        if args.category not in CATEGORIES:
            print(f"错误: 不支持的类别 '{args.category}'")
            print(f"可用类别: {', '.join(CATEGORIES.keys())}")
            return
        categories = [args.category]
    else:
        categories = list(CATEGORIES.keys())

    # 解析年份范围
    start_year = None
    end_year = None
    start_str = None
    end_str = None

    if args.start:
        # 支持 "2022" 或 "2024-Q1" 格式
        y = args.start.split("-")[0] if "-" in args.start else args.start
        start_year = int(y)
        start_str = args.start.replace("-", "")  # "2024-Q1" -> "2024Q1"
    if args.end:
        y = args.end.split("-")[0] if "-" in args.end else args.end
        end_year = int(y)
        end_str = args.end.replace("-", "")

    if args.download:
        # 下载模式
        print("\n登录 BaoStock...")
        lg = bs.login()
        if lg.error_code != '0':
            print(f"登录失败: {lg.error_msg}")
            return
        print("登录成功")

        try:
            download_all(code, args.data_dir, categories, start_year, end_year)
        finally:
            bs.logout()
    else:
        # 查询模式
        if not args.category or args.category == "all":
            print("查询模式请指定 --category，或使用 --list 查看可用类别")
            return

        if args.json:
            result = query_json(code, args.category, args.data_dir, start_str, end_str)
            print(json.dumps(result, ensure_ascii=False, indent=2))
        else:
            query_print(code, args.category, args.data_dir, start_str, end_str)


if __name__ == "__main__":
    main()
