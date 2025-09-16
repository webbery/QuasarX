import sys
import akshare as ak
import pandas as pd
import os

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

def update_fund(dst):
    csv_path = dst + '/fund_market.csv'
    data = None
    if os.path.exists(csv_path):
        data = pd.read_csv(csv_path)
    symbols = []
    types = []
    names = []
    for type_name in ["封闭式基金", "ETF基金", "LOF基金"]:
        fund_etf_category_df = ak.fund_etf_category_sina(symbol=type_name)
        for _, row in fund_etf_category_df.iterrows():
            code = row['代码']
            name = row['名称']
            types.append(type_name)
            symbols.append(code)
            names.append(name)
    etf = {'code': symbols, 'name': names, 'type': types}
    df = pd.DataFrame(data = etf)
    if data is not None:
        to_add = df[~df['code'].isin(data['code'])]
        df = pd.concat([data, to_add], ignore_index=True)
    df.to_csv(csv_path, index = False)

if __name__ == "__main__":
    task_id = int(sys.argv[1])
    if task_id == 1:    # 更新symbol_market.csv
        update_symbol_market('data/symbol_market.csv')
    elif task_id == 2:
        update_fund('data')