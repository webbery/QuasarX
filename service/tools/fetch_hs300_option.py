"""
获取沪深300股指期权数据
数据来源: AKShare option_finance_board

功能:
  1. 获取沪深300股指期权合约列表
  2. 获取期权实时行情数据
  3. 获取期权日K线数据
  4. 保存为 CSV/JSON 格式

用法:
  python fetch_hs300_option.py list                          # 获取合约列表
  python fetch_hs300_option.py quote                         # 获取实时行情
  python fetch_hs300_option.py kline --symbol IO2504-C-3800  # 获取K线数据
  python fetch_hs300_option.py all --save-dir ./data         # 获取全部数据并保存
"""
import akshare as ak
import pandas as pd
import json
import csv
import argparse
from datetime import datetime
from pathlib import Path
import sys


def get_option_symbols() -> pd.DataFrame:
    """
    获取沪深300股指期权合约列表
    
    Returns:
        DataFrame: 包含合约代码、期权类型、行权价等信息
    """
    try:
        # 使用 option_finance_board 获取沪深300股指期权合约列表
        symbol = "沪深300"
        df = ak.option_finance_board(symbol=symbol)
        return df
    except Exception as e:
        print(f"获取合约列表失败: {e}")
        return pd.DataFrame()


def get_option_quote(symbol: str = None) -> pd.DataFrame:
    """
    获取沪深300股指期权实时行情
    
    Args:
        symbol: 合约代码，如 "IO2504-C-3800"，为None则获取所有合约
        
    Returns:
        DataFrame: 期权实时行情数据
    """
    try:
        if symbol:
            # 获取特定合约的实时行情
            df = ak.option_finance_current(symbol=symbol)
        else:
            # 获取所有合约的实时行情
            df = ak.option_finance_current(symbol="沪深300")
        return df
    except Exception as e:
        print(f"获取实时行情失败: {e}")
        return pd.DataFrame()


def get_option_kline(symbol: str, period: str = "daily", start_date: str = None, end_date: str = None) -> pd.DataFrame:
    """
    获取期权K线数据
    
    Args:
        symbol: 合约代码，如 "IO2504-C-3800"
        period: 周期，"daily"/"weekly"/"monthly"
        start_date: 开始日期，格式 YYYY-MM-DD
        end_date: 结束日期，格式 YYYY-MM-DD
        
    Returns:
        DataFrame: K线数据
    """
    try:
        # 获取期权历史K线数据
        df = ak.option_finance_hist(symbol=symbol, period=period, start_date=start_date, end_date=end_date)
        return df
    except Exception as e:
        print(f"获取K线数据失败: {e}")
        return pd.DataFrame()


def save_to_csv(df: pd.DataFrame, filepath: Path):
    """保存数据到CSV文件"""
    filepath = Path(filepath)
    filepath.parent.mkdir(parents=True, exist_ok=True)
    df.to_csv(filepath, index=False, encoding='utf-8-sig')
    print(f"数据已保存到: {filepath}")


def save_to_json(df: pd.DataFrame, filepath: Path):
    """保存数据到JSON文件"""
    filepath = Path(filepath)
    filepath.parent.mkdir(parents=True, exist_ok=True)
    df.to_json(filepath, orient='records', force_ascii=False, indent=2)
    print(f"数据已保存到: {filepath}")


def print_symbols_table(df: pd.DataFrame):
    """打印合约列表表格"""
    if df.empty:
        print("无数据")
        return
    
    print("\n" + "=" * 100)
    print("沪深300股指期权合约列表")
    print("=" * 100)
    
    # 显示主要列
    display_cols = []
    for col in ['合约代码', '期权代码', '期权类型', '行权价', '到期日', '上市日']:
        if col in df.columns:
            display_cols.append(col)
    
    if not display_cols:
        display_cols = df.columns[:8].tolist()
    
    print(df[display_cols].to_string(index=False))
    print("=" * 100)
    print(f"共 {len(df)} 个合约\n")


def print_quote_table(df: pd.DataFrame):
    """打印实时行情表格"""
    if df.empty:
        print("无数据")
        return
    
    print("\n" + "=" * 120)
    print(f"沪深300股指期权实时行情 ({datetime.now().strftime('%Y-%m-%d %H:%M:%S')})")
    print("=" * 120)
    
    # 显示主要列
    display_cols = []
    for col in ['合约代码', '期权代码', '最新价', '涨跌幅', '买入价', '卖出价', '成交量', '持仓量', '行权价', '期权类型']:
        if col in df.columns:
            display_cols.append(col)
    
    if not display_cols:
        display_cols = df.columns[:12].tolist()
    
    print(df[display_cols].head(50).to_string(index=False))
    if len(df) > 50:
        print(f"... 共 {len(df)} 个合约，仅显示前50个")
    print("=" * 120)


def fetch_all_data(save_dir: str = "./data"):
    """获取所有数据并保存"""
    save_path = Path(save_dir)
    save_path.mkdir(parents=True, exist_ok=True)
    
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    
    # 1. 获取合约列表
    print("正在获取合约列表...")
    symbols_df = get_option_symbols()
    if not symbols_df.empty:
        save_to_csv(symbols_df, save_path / f"hs300_option_symbols_{timestamp}.csv")
        save_to_json(symbols_df, save_path / f"hs300_option_symbols_{timestamp}.json")
        print_symbols_table(symbols_df.head(20))
    
    # 2. 获取实时行情
    print("\n正在获取实时行情...")
    quote_df = get_option_quote()
    if not quote_df.empty:
        save_to_csv(quote_df, save_path / f"hs300_option_quote_{timestamp}.csv")
        save_to_json(quote_df, save_path / f"hs300_option_quote_{timestamp}.json")
        print_quote_table(quote_df)
    
    # 3. 获取主力合约K线数据 (前5个合约)
    if not symbols_df.empty:
        print("\n正在获取主力合约K线数据...")
        # 获取前5个合约的K线
        symbols_to_fetch = symbols_df.head(5)
        symbol_col = None
        for col in ['合约代码', '期权代码', 'symbol']:
            if col in symbols_to_fetch.columns:
                symbol_col = col
                break
        
        if symbol_col:
            for _, row in symbols_to_fetch.iterrows():
                symbol = row[symbol_col]
                print(f"  获取 {symbol} K线数据...")
                kline_df = get_option_kline(symbol=symbol)
                if not kline_df.empty:
                    save_to_csv(kline_df, save_path / f"hs300_option_kline_{symbol}_{timestamp}.csv")


def main():
    parser = argparse.ArgumentParser(description="获取沪深300股指期权数据")
    parser.add_argument(
        "action",
        nargs="?",
        choices=["list", "quote", "kline", "all"],
        default="list",
        help="操作类型: list-合约列表, quote-实时行情, kline-K线数据, all-全部数据"
    )
    parser.add_argument("--symbol", type=str, help="合约代码 (用于kline模式)")
    parser.add_argument("--period", type=str, default="daily", choices=["daily", "weekly", "monthly"], help="K线周期")
    parser.add_argument("--start-date", type=str, help="开始日期 (YYYY-MM-DD)")
    parser.add_argument("--end-date", type=str, help="结束日期 (YYYY-MM-DD)")
    parser.add_argument("--save-dir", type=str, default="./data", help="保存目录")
    parser.add_argument("--format", type=str, default="csv", choices=["csv", "json", "both"], help="保存格式")
    
    args = parser.parse_args()
    
    if args.action == "list":
        # 获取合约列表
        df = get_option_symbols()
        if not df.empty:
            print_symbols_table(df)
            
            # 保存数据
            save_path = Path(args.save_dir)
            if args.format in ["csv", "both"]:
                save_to_csv(df, save_path / "hs300_option_symbols.csv")
            if args.format in ["json", "both"]:
                save_to_json(df, save_path / "hs300_option_symbols.json")
    
    elif args.action == "quote":
        # 获取实时行情
        symbol = args.symbol
        df = get_option_quote(symbol)
        if not df.empty:
            print_quote_table(df)
            
            # 保存数据
            save_path = Path(args.save_dir)
            filename = f"hs300_option_quote_{symbol}" if symbol else "hs300_option_quote"
            if args.format in ["csv", "both"]:
                save_to_csv(df, save_path / f"{filename}.csv")
            if args.format in ["json", "both"]:
                save_to_json(df, save_path / f"{filename}.json")
    
    elif args.action == "kline":
        # 获取K线数据
        if not args.symbol:
            print("错误: kline模式需要指定 --symbol 参数")
            sys.exit(1)
        
        df = get_option_kline(
            symbol=args.symbol,
            period=args.period,
            start_date=args.start_date,
            end_date=args.end_date
        )
        
        if not df.empty:
            print(f"\n{args.symbol} K线数据 ({args.period})")
            print("=" * 100)
            print(df.to_string(index=False))
            print("=" * 100)
            
            # 保存数据
            save_path = Path(args.save_dir)
            filename = f"hs300_option_kline_{args.symbol}"
            if args.format in ["csv", "both"]:
                save_to_csv(df, save_path / f"{filename}.csv")
            if args.format in ["json", "both"]:
                save_to_json(df, save_path / f"{filename}.json")
    
    elif args.action == "all":
        # 获取全部数据
        fetch_all_data(args.save_dir)


if __name__ == "__main__":
    main()
