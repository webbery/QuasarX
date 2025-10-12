import akshare as ak
import argparse
import os
import pandas as pd
from datetime import datetime, date

cache_dir = '../data/cache'

def get_sector_today_flow():
    sector_fund_flow_df = ak.stock_sector_fund_flow_rank(indicator="今日")
    return sector_fund_flow_df

def merge_sector_df(org_df: pd.DataFrame, new_df: pd.DataFrame, date_column='日期'):
    """
    合并两个DataFrame，相同日期的数据保留旧数据
    
    参数:
    df_old: 原DataFrame（旧数据）
    df_new: 新DataFrame（新数据）
    date_column: 日期列名
    
    返回:
    合并后的DataFrame
    """
    # 设置日期列为索引
    df_old_indexed = org_df.set_index(date_column)
    df_new_indexed = new_df.set_index(date_column)
    
    # 使用combine_first，旧数据优先
    merged = df_old_indexed.combine_first(df_new_indexed)
    
    # 重置索引，恢复日期列
    merged = merged.reset_index()
    
    return merged

def is_last_date_today(df, date_column='日期'):
    """
    健壮地检查最后一行的日期是否是今天，处理各种日期格式
    """
    if df.empty or date_column not in df.columns:
        return False
    
    try:
        # 获取最后一行的日期
        last_date_value = df[date_column].iloc[-1]
        
        # 处理不同类型的日期格式
        if isinstance(last_date_value, (datetime, pd.Timestamp)):
            # 如果是datetime或Timestamp类型
            last_date = last_date_value.date()
        elif isinstance(last_date_value, str):
            # 如果是字符串，转换为日期
            last_date = pd.to_datetime(last_date_value).date()
        else:
            # 其他情况尝试转换
            last_date = pd.to_datetime(last_date_value).date()
        
        # 比较日期
        today = date.today()
        return last_date == today
        
    except Exception as e:
        print(f"错误: {e}")
        return False
    
def get_sector_history_flow(sector_name):
    # 如果历史数据存在,比较日期,然后更新最新数据
    sector_cache = cache_dir + '/sector'
    if not os.path.exists(sector_cache):
        os.makedirs(sector_cache, exist_ok=True)
    sector_file = sector_cache + '/' + sector_name + '.csv'
    if not os.path.exists(sector_file):
        # 首次下载
        sector_fund_flow_hist_df = ak.stock_sector_fund_flow_hist(symbol=sector_name)
        sector_fund_flow_hist_df.to_csv(sector_file, index=False)
        return sector_fund_flow_hist_df
    else:
        org_hist_df = pd.read_csv(sector_file)
        # 检查最后一行的日期列是否是今日，如果不是就下载数据
        if is_last_date_today(org_hist_df):
            return org_hist_df
        
        sector_fund_flow_hist_df = ak.stock_sector_fund_flow_hist(symbol=sector_name)
        new_df = merge_sector_df(org_df=org_hist_df, new_df=sector_fund_flow_hist_df)
        new_df.to_csv(sector_file, index=False)
        return new_df

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-t', "--type", type=str, default='')
    parser.add_argument('-s', "--sector", type=str, default='')
    args = parser.parse_args()

    if args.type == 'today':
        result = get_sector_today_flow()
        for _, row in result.iterrows():
            print(row['名称'], row['今日主力净流入-净额'], row['今日超大单净流入-净额'], row['今日大单净流入-净额'], row['今日中单净流入-净额'], row['今日小单净流入-净额'])
    else:
        sector_name = args.sector
        result = get_sector_history_flow(sector_name)
        for _, row in result.iterrows():
            print(row['日期'], row['主力净流入-净额'], row['超大单净流入-净额'], row['大单净流入-净额'], row['中单净流入-净额'], row['小单净流入-净额'])
