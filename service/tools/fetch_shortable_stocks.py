"""
获取融资融券标的（可做空标的）列表

数据来源:
  - 上交所 (SSE): akshare stock_margin_underlying_info_sse()
  - 深交所 (SZSE): akshare stock_margin_underlying_info_szse()

输出 JSON 格式:
{
  "updated_at": "2026-10-16T10:30:00",
  "data": [
    {"code": "sh.600000", "exchange": "SSE"},
    {"code": "sz.000001", "exchange": "SZSE"},
    ...
  ]
}

用法:
  python fetch_shortable_stocks.py              # 输出 JSON
  python fetch_shortable_stocks.py --summary    # 输出摘要信息
"""
import argparse
import sys
import json
from datetime import datetime

try:
    import akshare as ak
except ImportError:
    print("错误: 请先安装 akshare 库: pip install akshare")
    sys.exit(1)


def fetch_sse_margin_stocks():
    """获取上交所融资融券标的"""
    try:
        df = ak.stock_margin_underlying_info_sse()
        if df is None or df.empty:
            return []

        result = []
        for _, row in df.iterrows():
            # 上交所标的代码格式: 600000 -> sh.600000
            code = str(row.iloc[0]).strip()
            if len(code) == 6 and code.isdigit():
                result.append({
                    "code": f"sh.{code}",
                    "exchange": "SSE"
                })
        return result
    except Exception as e:
        print(f"获取上交所融资融券标的失败: {e}", file=sys.stderr)
        return []


def fetch_szse_margin_stocks():
    """获取深交所融资融券标的"""
    try:
        df = ak.stock_margin_underlying_info_szse()
        if df is None or df.empty:
            return []

        result = []
        for _, row in df.iterrows():
            # 深交所标的代码格式: 000001 -> sz.000001
            code = str(row.iloc[0]).strip()
            if len(code) == 6 and code.isdigit():
                result.append({
                    "code": f"sz.{code}",
                    "exchange": "SZSE"
                })
        return result
    except Exception as e:
        print(f"获取深交所融资融券标的失败: {e}", file=sys.stderr)
        return []


def fetch_all_margin_stocks():
    """获取全部融资融券标的（上交所 + 深交所）"""
    sse_stocks = fetch_sse_margin_stocks()
    szse_stocks = fetch_szse_margin_stocks()

    all_stocks = sse_stocks + szse_stocks

    return {
        "updated_at": datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
        "data": all_stocks
    }


def print_summary(data):
    """打印摘要信息"""
    stocks = data.get("data", [])
    sse_count = sum(1 for s in stocks if s["exchange"] == "SSE")
    szse_count = sum(1 for s in stocks if s["exchange"] == "SZSE")

    print("=" * 60)
    print("融资融券标的（可做空标的）列表")
    print("=" * 60)
    print(f"更新时间: {data.get('updated_at', 'N/A')}")
    print(f"总数: {len(stocks)}")
    print(f"  上交所 (SSE): {sse_count}")
    print(f"  深交所 (SZSE): {szse_count}")
    print("-" * 60)

    # 显示前 10 个示例
    print("示例标的:")
    for s in stocks[:10]:
        print(f"  {s['code']}  ({s['exchange']})")
    if len(stocks) > 10:
        print(f"  ... 共 {len(stocks)} 只")

    print("=" * 60)


def main():
    parser = argparse.ArgumentParser(
        description="获取融资融券标的（可做空标的）列表",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python fetch_shortable_stocks.py           # 输出 JSON 格式
  python fetch_shortable_stocks.py --summary # 输出摘要信息
        """
    )
    parser.add_argument("--summary", action="store_true", help="输出摘要信息（非 JSON）")

    args = parser.parse_args()

    data = fetch_all_margin_stocks()

    if args.summary:
        print_summary(data)
    else:
        print(json.dumps(data, ensure_ascii=False))


if __name__ == "__main__":
    main()
