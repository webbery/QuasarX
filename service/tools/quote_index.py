import akshare as ak
import sys
import pandas as pd

def get_index_rt_value(index):
    rows = []
    # if index % 2 == 0:
    data = ak.stock_zh_index_spot_em('沪深重要指数')
    for _, row in data.iterrows():
        if row['代码'] in ['000001', '399001', '899050', '399006']:
            rows.append([row['代码'], row['最新价'], row['涨跌幅']])
    # else:
    #     data = ak.stock_zh_index_spot_sina()
    #     for _, row in data.iterrows():
    #         if row['代码'] in ['sh000001', 'sz399001']:
    #             rows.append([row['代码'], row['最新价'], row['涨跌幅']])

    return pd.DataFrame(data=rows)

if __name__ == "__main__":
    index = int(sys.argv[1])
    v = get_index_rt_value(index)
    print(v)