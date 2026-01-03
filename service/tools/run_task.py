import sys
import akshare as ak
import pandas as pd
import os

def update_symbol_market(dst):
    codes = []
    market = []
    names = []
    industries = []
    types = []

    stock_list_df = ak.stock_info_a_code_name()
    symbol_info = dict()
    for idx, row in stock_list_df.iterrows():
        symbol = str(row['code']).zfill(6)
        # info = ak.stock_individual_info_em(symbol)
        name = row['name'].replace("*ST", "")
        symbol_info[symbol] = name
        # for _, item in info.iterrows():
        #     if item['item'] == '行业':
        #         symbol_info[symbol] = (name, item['value'])
        #         break

    sz_A = ak.stock_info_sz_name_code()
    bk_dict = {
        '主板': 'A', '创业板': 'CYB',
    }
    for _, row in sz_A.iterrows():
        symbol = str(row['A股代码']).zfill(6)
        codes.append(symbol)
        market.append('SZ')
        bk = row['板块']
        types.append(bk_dict[bk])
        if symbol_info[symbol] is None:
            names.append('')
            continue

        names.append(symbol_info[symbol])
    
    for type in [('主板A股', 'A'), ("主板B股", 'B'), ("科创板", "KCB")]:
        sh_A = ak.stock_info_sh_name_code(symbol=type[0])
        for _, row in sh_A.iterrows():
            symbol = str(row['证券代码']).zfill(6)
            codes.append(symbol)
            market.append('SH')
            types.append(type[1])
            if symbol_info[symbol] is None:
                names.append('')
                continue

            names.append(symbol_info[symbol])
    # sh_kc = ak.stock_info_sh_name_code(symbol='科创板')
    bj_A = ak.stock_info_bj_name_code()
    for _, row in bj_A.iterrows():
        symbol = str(row['证券代码']).zfill(6)
        codes.append(symbol)
        market.append('BJ')
        types.append('-')
        if symbol_info[symbol] is None:
            names.append('')
            continue

        names.append(symbol_info[symbol])

    df = pd.DataFrame({
        '代码': codes,
        '交易所': market,
        "name": names,
        'market': types
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

def update_option(dst):
    csv_path = dst + '/option_market.csv'
    option_contract_info_ctp_df = ak.option_contract_info_ctp()
    option_contract_info_ctp_df.to_csv(csv_path, index=False)

if __name__ == "__main__":
    task_id = int(sys.argv[1])
    if task_id == 1:    # 更新symbol_market.csv
        update_symbol_market('data/symbol_market.csv')
    elif task_id == 2:
        update_fund('data')
    elif task_id == 3:  # 更新期权
        update_option('data')