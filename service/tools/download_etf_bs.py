"""
使用 BaoStock 下载 ETF 分钟级/日线历史数据

数据源: BaoStock (http://baostock.com)
优点: 免费、稳定、官方数据源、不易被封
限制: 不支持1m，分钟数据约3个月历史，日线数据完整
      支持频率: 5m, 15m, 30m, 60m, daily

用法:
  # 日线数据（完整历史）
  python download_etf_bs.py 510300.SH build/data --freq daily

  # 5分钟数据（约3个月）
  python download_etf_bs.py 510300.SH build/data --freq 5m

  # 多只ETF
  python download_etf_bs.py "510300.SH,159919.SZ" build/data --freq 5m

  # 指定日期范围
  python download_etf_bs.py 510300.SH build/data --freq 5m --start 2024-01-01 --end 2024-12-31
"""
import argparse
import os
import sys
import time
from datetime import datetime, timedelta
from pathlib import Path

try:
    import baostock as bs
    import pandas as pd
except ImportError:
    print("错误: 请先安装 baostock 和 pandas:")
    print("  pip install baostock pandas")
    sys.exit(1)


# ============================================================
# 代码格式转换
# ============================================================

def convert_to_baostock_code(symbol: str) -> str:
    """
    将多种格式转为 baostock 格式 (sh.510300 / sz.159919)

    支持输入:
      "510300.SH" -> "sh.510300"
      "sh.510300" -> "sh.510300"
      "510300"    -> "sh.510300" (按前缀推断)
      "159919.SZ" -> "sz.159919"
    """
    sym = symbol.strip().upper()

    # 510300.SH / 159919.SZ 格式
    if "." in sym:
        parts = sym.split(".")
        code = parts[0]
        market = parts[1]
        if market in ("SH", "SSE"):
            return f"sh.{code}"
        elif market in ("SZ", "SZSE"):
            return f"sz.{code}"
        return f"{market.lower()}.{code}"

    # 纯数字代码
    code = sym
    if code.startswith(("51", "56", "58")):
        return f"sh.{code}"
    elif code.startswith(("15", "16")):
        return f"sz.{code}"
    else:
        return f"sh.{code}"


def to_code_exchange(symbol: str) -> str:
    """
    将 BaoStock 格式或大写交易所格式转为 CODE.EXCHANGE 格式
    如 "sh.510300" -> "510300.SH"，"sz.159919" -> "159919.SZ"
    如果已经是 CODE.EXCHANGE 格式则直接返回
    """
    sym = symbol.strip()
    if "." not in sym:
        return sym  # 纯代码，原样返回

    parts = sym.split(".")
    if len(parts) != 2:
        return sym

    left, right = parts[0], parts[1].upper()

    # 如果左边是数字，右边是交易所 -> 已经是 CODE.EXCHANGE 格式
    if left.isdigit():
        return sym.upper() if right in ("SH", "SZ", "SSE", "SZSE", "BJ") else sym

    # BaoStock 格式: sh.510300 -> 510300.SH
    exchange_map = {"sh": "SH", "sse": "SH", "sz": "SZ", "szse": "SZ", "bj": "BJ"}
    exchange = exchange_map.get(left.lower(), right)
    return f"{left}.{exchange}"


# ============================================================
# 频率映射
# ============================================================

FREQ_MAP = {
    "daily": "d",
    # "1m": "1",  # BaoStock 不支持 1m
    "5m": "5",
    "15m": "15",
    "30m": "30",
    "60m": "60",
}


# ============================================================
# 数据下载
# ============================================================

def download_etf_data(
    symbol: str,
    freq: str = "daily",
    start_date: str = None,
    end_date: str = None,
    adjust: str = "3",
) -> pd.DataFrame:
    """
    使用 BaoStock 下载 ETF 数据

    Args:
        symbol: baostock 格式代码 (如 "sh.510300")
        freq: 频率 daily/1m/5m/15m/30m/60m
        start_date: 开始日期 YYYY-MM-DD
        end_date: 结束日期
        adjust: 1=后复权 2=前复权 3=不复权

    Returns:
        DataFrame
    """
    frequency = FREQ_MAP.get(freq, "d")

    if not start_date:
        if frequency == "d":
            start_date = "2010-01-01"
        else:
            start_date = (datetime.now() - timedelta(days=90)).strftime("%Y-%m-%d")

    if not end_date:
        end_date = datetime.now().strftime("%Y-%m-%d")

    # 字段
    if frequency == "d":
        fields = "date,open,high,low,close,volume,amount,turn"
    else:
        fields = "date,time,open,high,low,close,volume,amount,turn"

    try:
        rs = bs.query_history_k_data_plus(
            symbol,
            fields,
            start_date=start_date,
            end_date=end_date,
            frequency=frequency,
            adjustflag=adjust,
        )

        if rs.error_code != '0':
            print(f"  [{symbol}] 查询失败: {rs.error_msg}")
            return pd.DataFrame()

        df = rs.get_data()

        if df.empty:
            print(f"  [{symbol}] 无数据 ({freq}, {start_date} ~ {end_date})")
            return pd.DataFrame()

        return df

    except Exception as e:
        print(f"  [{symbol}] 下载异常: {e}")
        return pd.DataFrame()


# ============================================================
# 数据标准化
# ============================================================

def normalize_columns(df: pd.DataFrame, freq: str) -> pd.DataFrame:
    """标准化列名，合并 date + time -> datetime"""
    if df.empty:
        return df

    col_map = {
        'date': 'datetime',
        'time': '_time',
        'open': 'open',
        'high': 'high',
        'low': 'low',
        'close': 'close',
        'volume': 'volume',
        'turn': 'turnover',
    }
    df = df.rename(columns=col_map)

    # 分钟数据: 合并 date + time
    if freq != "daily" and '_time' in df.columns:
        # BaoStock time 格式: YYYYMMDDHHMMSS000
        # 取前14位 -> YYYY-MM-DD HH:MM:SS
        def parse_bstime(row):
            t = str(row['_time'])
            if len(t) >= 14:
                return f"{t[:4]}-{t[4:6]}-{t[6:8]} {t[8:10]}:{t[10:12]}:{t[12:14]}"
            return str(row['datetime']) + " 09:30:00"

        df['datetime'] = df.apply(parse_bstime, axis=1)
        df = df.drop(columns=['_time'])

    df['datetime'] = pd.to_datetime(df['datetime'])

    required = ['datetime', 'open', 'high', 'low', 'close', 'volume', 'turnover']
    for col in required:
        if col not in df.columns:
            return pd.DataFrame()

    df = df[required].copy()
    df = df.sort_values('datetime').reset_index(drop=True)

    for col in ['open', 'high', 'low', 'close', 'volume', 'turnover']:
        df[col] = pd.to_numeric(df[col], errors='coerce')

    return df


# ============================================================
# CSV 输出
# ============================================================

def save_csv(df: pd.DataFrame, filepath: str, use_timestamp: bool = False):
    """
    保存 CSV

    use_timestamp=False: Python 格式 (YYYY-MM-DD HH:MM:SS)
    use_timestamp=True:  C++ 格式 (Unix 时间戳)
    """
    if df is None or df.empty:
        return False

    os.makedirs(os.path.dirname(filepath), exist_ok=True)

    if use_timestamp:
        # C++ 格式: datetime 为 Unix 时间戳
        lines = []
        for _, row in df.iterrows():
            ts = int(pd.Timestamp(row['datetime']).timestamp())
            lines.append(
                f"{ts},{row['open']:.4f},{row['close']:.4f},"
                f"{row['high']:.4f},{row['low']:.4f},{int(row['volume'])},"
                f"{row['turnover']:.4f}"
            )
        csv_content = "datetime,open,close,high,low,volume,turnover\n" + "\n".join(lines)
        with open(filepath, "w", encoding="utf-8") as f:
            f.write(csv_content)
    else:
        # Python 格式: 标准 datetime 列
        out_cols = ['datetime', 'open', 'close', 'high', 'low', 'volume', 'turnover']
        df[out_cols].to_csv(filepath, index=False)

    label = "C++" if use_timestamp else "Python"
    print(f"  [OK] {label} 格式: {filepath} ({len(df)} 条)")
    return True


# ============================================================
# 单只标的下载
# ============================================================

def download_single_data(symbol, data_dir, freq, start_date, end_date, asset_type="etf"):
    """下载后复权 + 不复权数据"""
    bs_code = convert_to_baostock_code(symbol)
    print(f"\n[{bs_code}] 下载 {freq}...")

    success = False

    for adjust_flag, subdir, label in [
        ("1", f"{asset_type}_hfq", "后复权"),
        ("3", f"{asset_type}_org", "不复权"),
    ]:
        df = download_etf_data(
            symbol=bs_code, freq=freq,
            start_date=start_date, end_date=end_date,
            adjust=adjust_flag,
        )

        if df.empty:
            print(f"  [{bs_code}] {label} 无数据")
            continue

        df = normalize_columns(df, freq)
        if df.empty:
            continue

        out_dir = os.path.join(data_dir, subdir, freq)
        os.makedirs(out_dir, exist_ok=True)

        # 文件名统一使用 CODE.EXCHANGE.csv 格式（如 510300.SH.csv / 000001.SZ.csv）
        # 注意：使用 bs_code（已转为 sh.510300 格式）保证一致性，避免输入格式不统一
        csv_name = to_code_exchange(bs_code)
        filename = f"{csv_name}.csv"

        # 只保存 Python 格式 (YYYY-MM-DD HH:MM:SS)
        save_csv(df, os.path.join(out_dir, filename), use_timestamp=False)

        success = True
        time.sleep(0.5)

    return success


# ============================================================
# 主流程
# ============================================================

def main():
    parser = argparse.ArgumentParser(
        description="使用 BaoStock 下载 ETF 历史数据",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("symbols", nargs="?", default="510300.SH")
    parser.add_argument("data_dir", nargs="?", default="build/data")
    parser.add_argument("--freq", default="5m",
                        choices=["daily", "5m", "15m", "30m", "60m"],
                        help="BaoStock 不支持 1m 数据")
    parser.add_argument("--start", default=None, help="开始日期 YYYY-MM-DD")
    parser.add_argument("--end", default=None, help="结束日期 YYYY-MM-DD")
    parser.add_argument("--asset-type", default="etf", choices=["etf", "stock"],
                        help="资产类型: etf / stock")

    args = parser.parse_args()
    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
    asset_label = "ETF" if args.asset_type == "etf" else "Stock"

    print("=" * 60)
    print(f"{asset_label} 历史数据下载 (BaoStock)")
    print("=" * 60)
    print(f"标的: {', '.join(symbols)}")
    print(f"频率: {args.freq}")
    print(f"目录: {args.data_dir}")
    if args.start: print(f"开始: {args.start}")
    if args.end:   print(f"结束: {args.end}")
    print("=" * 60)

    # 登录
    print("\n登录 BaoStock...")
    lg = bs.login()
    if lg.error_code != '0':
        print(f"登录失败: {lg.error_msg}")
        return
    print("登录成功")

    try:
        ok_count = 0
        for sym in symbols:
            if download_single_data(
                sym, args.data_dir, args.freq, args.start, args.end, args.asset_type
            ):
                ok_count += 1
            if len(symbols) > 1:
                time.sleep(1)

        print(f"\n完成: {ok_count}/{len(symbols)} 成功")
        if args.freq != "daily":
            print("[WARN] 分钟数据约3个月历史")

    finally:
        bs.logout()


if __name__ == "__main__":
    main()
