import akshare as ak
import argparse

def get_sector_today_flow():
    sector_fund_flow_df = ak.stock_sector_fund_flow_rank(indicator="今日")
    return sector_fund_flow_df

def get_sector_history_flow():
    pass

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument('-t', "--type", type=str, default='today')
    args = parser.parse_args()

    if args.type == 'today':
        result = get_sector_today_flow()
        for _, row in result.iterrows():
            print(row['名称'], row['今日主力净流入-净额'], row['今日超大单净流入-净额'], row['今日大单净流入-净额'], row['今日中单净流入-净额'], row['今日小单净流入-净额'])
    else:
        result = get_sector_history_flow()