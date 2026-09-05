#!/usr/bin/env python3
"""
从上交所获取 ETF 期权日终行情数据 (akshare 新浪源)

数据源: akshare (新浪财经)
品种: 50ETF(510050), 沪深300ETF(510300), 500ETF(510500), 科创50ETF(588000)

注: 旧版走上交所 yunhq.sse.com.cn:32041 ZIP 接口已于 2026-09 失效 (503),
    本脚本改用 akshare option_sse_daily_sina (新浪源) 逐合约获取日线数据.

限制:
  - 只能获取当前仍存续的合约历史 (已到期摘牌的合约无法查询)
  - 新浪源不提供 settlement / prev_settlement / open_interest 字段

用法:
  python fetch_sse_option.py --start 2026-01-01 --end 2026-09-06 --product 50ETF
  python fetch_sse_option.py --start 2026-07-01 --end 2026-09-06  # 全部品种
  python fetch_sse_option.py --start 2026-07-01 --end 2026-09-06 --save-dir data/option/sse

输出:
  {save_dir}/{product}.csv  每个品种一个 CSV, 包含该品种所有合约的日线数据
  列: trade_date, product, contract_code, contract_name, call_put,
      strike_price, open, high, low, close, volume
"""
import argparse
import sys
import time
from datetime import datetime, date
from pathlib import Path
from typing import Optional

try:
    import pandas as pd
except ImportError as e:
    print(f"错误: 缺少依赖 {e.name}: pip install pandas")
    sys.exit(1)

try:
    import akshare as ak
except ImportError as e:
    print(f"错误: 缺少依赖 {e.name}: pip install akshare")
    sys.exit(1)


# 品种 → 标的代码映射
PRODUCT_UNDERLYING = {
    "50ETF":      {"code": "510050", "name": "上证50ETF"},
    "300ETF":     {"code": "510300", "name": "沪深300ETF"},
    "500ETF":     {"code": "510500", "name": "中证500ETF"},
    "STAR50ETF":  {"code": "588000", "name": "科创50ETF"},
}


def fetch_product(product: str, underlying_code: str,
                  start_date: Optional[date], end_date: Optional[date],
                  save_dir: Path) -> int:
    """
    获取某品种全部合约日线数据, 保存为 CSV.
    返回: 导入行数
    """
    print(f"  -> {product}: 获取合约列表 (标的={underlying_code})...")

    # 获取当前全部 SSE 期权合约
    all_contracts = ak.option_current_day_sse()
    if all_contracts.empty:
        print(f"  [WARN] {product}: 无当前合约")
        return 0

    # 筛选该品种的合约 (标的券名称及代码 包含 underlying_code)
    mask = all_contracts['标的券名称及代码'].str.contains(underlying_code, na=False)
    contracts = all_contracts[mask].copy()
    print(f"  -> {product}: 找到 {len(contracts)} 个合约")

    if contracts.empty:
        return 0

    all_rows = []
    failed = 0

    for i, (_, ctr) in enumerate(contracts.iterrows()):
        contract_code = str(ctr['合约编码'])
        contract_name = str(ctr.get('合约简称', contract_code))
        call_put_raw = str(ctr.get('类型', ''))
        strike_price = ctr.get('行权价', '')
        start_dt_str = str(ctr.get('开始日期', ''))

        # 类型映射: 认购 → call, 认沽 → put
        call_put = 'call' if '认购' in call_put_raw else ('put' if '认沽' in call_put_raw else call_put_raw)

        try:
            daily = ak.option_sse_daily_sina(symbol=contract_code)
        except Exception as e:
            failed += 1
            if failed <= 3:
                print(f"    [WARN] {contract_code} ({contract_name}) 获取失败: {e}")
            continue

        if daily is None or daily.empty:
            continue

        # 按日期过滤
        daily['日期'] = pd.to_datetime(daily['日期'])
        if start_date:
            daily = daily[daily['日期'] >= pd.Timestamp(start_date)]
        if end_date:
            daily = daily[daily['日期'] <= pd.Timestamp(end_date)]

        if daily.empty:
            continue

        # 构建输出行
        for _, row in daily.iterrows():
            all_rows.append({
                'trade_date': row['日期'].strftime('%Y-%m-%d'),
                'product': product,
                'contract_code': contract_code,
                'contract_name': contract_name,
                'call_put': call_put,
                'strike_price': strike_price,
                'open': row.get('开盘', ''),
                'high': row.get('最高', ''),
                'low': row.get('最低', ''),
                'close': row.get('收盘', ''),
                'volume': row.get('成交量', ''),
            })

        # 进度提示 (每 20 个合约打印一次)
        if (i + 1) % 20 == 0:
            print(f"    已处理 {i + 1}/{len(contracts)} 个合约, 累计 {len(all_rows)} 行...")

        time.sleep(0.3)  # 避免被限频

    if not all_rows:
        print(f"  [WARN] {product}: 无数据 (合约可能均已到期或日期范围无覆盖)")
        return 0

    df = pd.DataFrame(all_rows)
    out_path = save_dir / f"{product}.csv"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(out_path, index=False, encoding='utf-8-sig')

    print(f"  [OK] {product}: {len(df)} 行, {df['contract_code'].nunique()} 个合约 -> {out_path.name}"
          + (f" ({failed} 个合约获取失败)" if failed else ""))
    return len(df)


def main():
    parser = argparse.ArgumentParser(
        description="获取 SSE ETF 期权日终行情 (akshare 新浪源)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--start",     type=str, help="起始日期 YYYY-MM-DD")
    parser.add_argument("--end",       type=str, help="结束日期 YYYY-MM-DD")
    parser.add_argument("--product",   type=str,
                        choices=list(PRODUCT_UNDERLYING.keys()) + ["all"],
                        default="all", help="品种，默认 all")
    parser.add_argument("--save-dir",  type=str, default="data/option_sse",
                        help="CSV 保存目录")
    args = parser.parse_args()

    start_date = datetime.strptime(args.start, "%Y-%m-%d").date() if args.start else None
    end_date = datetime.strptime(args.end, "%Y-%m-%d").date() if args.end else None

    products = list(PRODUCT_UNDERLYING.keys()) if args.product == "all" else [args.product]
    save_dir = Path(args.save_dir)
    save_dir.mkdir(parents=True, exist_ok=True)

    print(f"开始获取 SSE ETF 期权 (akshare): 品种={products}, "
          f"日期={args.start or '不限'} ~ {args.end or '不限'}")
    print(f"保存目录: {save_dir.absolute()}")
    print("=" * 70)

    total_rows = 0
    for prod in products:
        cfg = PRODUCT_UNDERLYING[prod]
        rows = fetch_product(prod, cfg["code"], start_date, end_date, save_dir)
        total_rows += rows
        time.sleep(1)

    print("=" * 70)
    print(f"完成: 累计 {total_rows} 行")


if __name__ == "__main__":
    main()
