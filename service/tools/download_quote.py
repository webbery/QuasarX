import akshare as ak
import pandas as pd
import os
import sys
import datetime

class StockQuoteDownloader:
    """
    股票行情数据下载器
    支持下载日线行情（可设置复权类型），从新浪财经获取数据，合并历史数据
    """

    def __init__(self, symbol, dir, type):
        """
        初始化下载器
        :param symbol: 股票代码，如 "600000" 或 "sh600000"
        :param dir: 数据保存目录
        :param type: 复权类型，可选值: '' (不复权), 'qfq' (前复权), 'hfq' (后复权)
        """
        self.market = symbol[:2]
        self.symbol = symbol[2:]
        self.dir = dir
        self.type = type if type in ('', 'qfq', 'hfq') else ''  # 默认不复权

        # 确保保存目录存在
        if not os.path.exists(dir):
            os.makedirs(dir)

        # 定义标准列名映射（中文 -> 英文）
        self.column_map = {
            '日期': 'date',
            '开盘': 'open',
            '收盘': 'close',
            '最高': 'high',
            '最低': 'low',
            '成交量': 'volume'
        }

        self.start_date="20100101"

    def download(self):
        """
        从常规接口（akshare stock_zh_a_hist）下载1级行情数据
        保留 open/close/high/low/volume 列，并保存为 CSV 文件
        文件名格式: {symbol}_{type}.csv
        :return: DataFrame 包含标准列，若失败返回 None
        """
        end_date = datetime.datetime.now().strftime("%Y%m%d")
        try:
            # 调用 akshare 获取历史数据，adjust 参数控制复权类型
            df = ak.stock_zh_a_hist(symbol=self.symbol, period='daily', start_date=self.start_date, end_date=end_date, adjust=self.type)
            if df.empty:
                print(f"警告：未获取到 {self.symbol} 的数据")
                return None

            # 重命名列并保留所需字段
            df = df.rename(columns=self.column_map)
            df = df[list(self.column_map.values())]

            # 日期列转为 datetime 类型，并设为索引
            df['date'] = pd.to_datetime(df['date'])
            df.set_index('date', inplace=True)
            df.sort_index(inplace=True)

            # 保存到文件
            filename = f"{self.symbol}.csv"
            filepath = self.dir + '/' + filename
            df.to_csv(filepath, index=False)
            print(f"数据已保存至: {filepath}")
            return df

        except Exception as e:
            print(f"下载失败: {e}")
            return None

    def download_from_sina(self):
        """
        从新浪财经接口下载原始行情数据（通常不复权）
        保存为中文列名的 CSV，文件名: {symbol}_sina.csv
        :return: 原始 DataFrame（中文列名），若失败返回 None
        """
        end_date = datetime.datetime.now().strftime("%Y%m%d")
        try:
            # 需要加前缀如sz
            df = ak.stock_zh_a_daily(symbol=(self.market + self.symbol), start_date=self.start_date, end_date=end_date, adjust=self.type)

            if df.empty:
                print(f"警告：新浪接口未返回 {self.symbol} 的数据")
                return None

            # 新浪返回的列可能包含：日期,开盘,最高,最低,收盘,成交量
            df = df.rename(columns=self.column_map)
            df = df[list(self.column_map.values())]
            # 无需重命名，直接保存原始格式
            filename = f"{self.market}.{self.symbol}.csv"
            filepath = self.dir + "/" + filename
            df.to_csv(filepath, index=False)
            print(f"新浪原始数据已保存至: {filepath}")
            return df

        except Exception as e:
            print(f"从新浪下载失败: {e}")
            return None

    def convert_sina(self, sina_df=None):
        """
        将新浪原始数据转换为标准格式（英文列名，日期索引）
        如果未传入 DataFrame，则尝试读取本地新浪文件
        :param sina_df: 可选，新浪原始数据的 DataFrame
        :return: 转换后的标准 DataFrame，若失败返回 None
        """
        try:
            if sina_df is None:
                # 尝试读取本地新浪文件
                sina_file = self.dir / f"{self.symbol}_sina.csv"
                if not sina_file.exists():
                    print(f"未找到新浪数据文件: {sina_file}")
                    return None
                sina_df = pd.read_csv(sina_file)

            if sina_df.empty:
                print("新浪数据为空")
                return None

            # 重命名列（注意：新浪列名可能与预期略有不同，需适配）
            # 假设新浪返回列名：'日期','开盘','最高','最低','收盘','成交量'
            df = sina_df.rename(columns=self.column_map)

            # 确保只有标准列
            df = df[list(self.column_map.values())]

            # 日期处理
            df['date'] = pd.to_datetime(df['date'])
            df.set_index('date', inplace=True)
            df.sort_index(inplace=True)

            # 保存为标准格式文件（带 _sina_std 后缀）
            std_filename = f"{self.symbol}_sina_std.csv"
            std_filepath = self.dir / std_filename
            df.to_csv(std_filepath)
            print(f"转换后的新浪数据已保存至: {std_filepath}")
            return df

        except Exception as e:
            print(f"转换新浪数据失败: {e}")
            return None

    def merge_history(self, primary_df=None, sina_df=None):
        """
        合并主数据（常规接口）与新浪数据，去重并按日期排序
        默认读取本地文件：主文件 {symbol}_{type}.csv 或 {symbol}.csv，
        新浪标准文件 {symbol}_sina_std.csv
        合并后保存回主文件（覆盖）
        :param primary_df: 可选，主数据的 DataFrame
        :param sina_df: 可选，已转换的新浪标准数据 DataFrame
        :return: 合并后的 DataFrame，若失败返回 None
        """
        try:
            # 确定主文件路径
            if self.type:
                primary_file = self.dir / f"{self.symbol}_{self.type}.csv"
            else:
                primary_file = self.dir / f"{self.symbol}.csv"

            # 如果未提供 primary_df 且文件存在，则读取
            if primary_df is None and primary_file.exists():
                primary_df = pd.read_csv(primary_file, index_col=0, parse_dates=True)

            # 如果未提供 sina_df 且新浪标准文件存在，则读取
            std_sina_file = self.dir / f"{self.symbol}_sina_std.csv"
            if sina_df is None and std_sina_file.exists():
                sina_df = pd.read_csv(std_sina_file, index_col=0, parse_dates=True)

            # 收集所有要合并的 DataFrame
            dfs_to_merge = []
            if primary_df is not None and not primary_df.empty:
                dfs_to_merge.append(primary_df)
            if sina_df is not None and not sina_df.empty:
                dfs_to_merge.append(sina_df)

            if not dfs_to_merge:
                print("没有可合并的数据")
                return None

            # 合并、去重、排序
            merged = pd.concat(dfs_to_merge)
            merged = merged[~merged.index.duplicated(keep='first')]  # 去重，保留第一个出现的
            merged.sort_index(inplace=True)

            # 保存回主文件
            merged.to_csv(primary_file)
            print(f"合并后的数据已保存至: {primary_file}")
            return merged

        except Exception as e:
            print(f"合并数据失败: {e}")
            return None
        
if __name__ == "__main__":
    symbol = sys.argv[1]
    dir = sys.argv[2]
    downloader = StockQuoteDownloader(symbol, dir, 'hfq')
    downloader.download_from_sina()