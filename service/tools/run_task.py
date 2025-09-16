import sys
import akshare as ak
import pandas as pd

def update_symbol_market(dst):
    codes = []
    market = []
    sz_A = ak.stock_info_sz_name_code()
    for _, row in sz_A.iterrows():
        symbol = str(row['A股代码']).zfill(6)
        codes.append(symbol)
        market.append('SZ')
    
    sh_A = ak.stock_info_sh_name_code(symbol='主板A股')
    for _, row in sh_A.iterrows():
        symbol = str(row['证券代码']).zfill(6)
        codes.append(symbol)
        market.append('SH')
    # sh_kc = ak.stock_info_sh_name_code(symbol='科创板')
    bj_A = ak.stock_info_bj_name_code()
    for _, row in bj_A.iterrows():
        symbol = str(row['证券代码']).zfill(6)
        codes.append(symbol)
        market.append('BJ')
    df = pd.DataFrame({
        '代码': codes,
        '交易所': market
    })
    df.to_csv(dst, index=False)

if __name__ == "__main__":
    task_id = int(sys.argv[1])
    if task_id == 1:    # 更新symbol_market.csv
        update_symbol_market('data/symbol_market.csv')