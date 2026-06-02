"""
获取中国/全球宏观经济指标数据

支持指标:
  中国: CPI, PPI, GDP, PMI, M2, 社融, 失业率, 进出口, 零售销售, 工业产出
  美国: CPI, 非农, 失业率, GDP, 联邦基金利率
  全球: 各国GDP, 通胀, 利率

依赖:
  pip install akshare

用法:
  python fetch_macro_data.py --indicator cpi --country china
  python fetch_macro_data.py --indicator pmi --country china --start 2024-01-01 --end 2026-01-01
  python fetch_macro_data.py --indicator cpi --country usa
  python fetch_macro_data.py --list  # 列出所有支持的指标
"""
import argparse
import sys
from datetime import datetime

try:
    import akshare as ak
except ImportError:
    print("错误: 请先安装 akshare 库: pip install akshare")
    sys.exit(1)


# ============================================================
# 指标映射表: (country, indicator) -> fetch_function
# ============================================================

MACRO_INDICATORS = {
    # === 中国宏观 ===
    ("china", "cpi"): {
        "name": "中国居民消费价格指数(CPI)",
        "fetch": lambda: ak.macro_china_cpi_yearly(),
        "columns": {"日期": "date", "当月": "cpi"},
    },
    ("china", "ppi"): {
        "name": "中国工业生产者出厂价格指数(PPI)",
        "fetch": lambda: ak.macro_china_ppi_yearly(),
        "columns": {"日期": "date", "当月": "ppi"},
    },
    ("china", "gdp"): {
        "name": "中国国内生产总值(GDP)",
        "fetch": lambda: ak.macro_china_gdp_yearly(),
        "columns": {"年份": "year", "国内生产总值": "gdp"},
    },
    ("china", "pmi"): {
        "name": "中国采购经理人指数(PMI)",
        "fetch": lambda: ak.macro_china_pmi_yearly(),
        "columns": {"日期": "date", "当月": "pmi"},
    },
    ("china", "m2"): {
        "name": "中国广义货币供应(M2)",
        "fetch": lambda: ak.macro_china_money_supply(),
        "columns": {"年月": "date", "M2(亿元)": "m2"},
    },
    ("china", "social_financing"): {
        "name": "中国社会融资规模",
        "fetch": lambda: ak.macro_china_social_financing_flow(),
        "columns": {"年月": "date", "社会融资规模": "social_financing"},
    },
    ("china", "unemployment"): {
        "name": "中国城镇调查失业率",
        "fetch": lambda: ak.macro_china_urban_unemployment_rate_yearly(),
        "columns": {"月份": "date", "失业率": "unemployment"},
    },
    ("china", "trade"): {
        "name": "中国进出口贸易差额",
        "fetch": lambda: ak.macro_china_trade_balance(),
        "columns": {"月份": "date", "贸易差额(亿美元)": "trade_balance"},
    },
    ("china", "retail_sales"): {
        "name": "中国社会消费品零售总额",
        "fetch": lambda: ak.macro_china_retail_sales_yoy(),
        "columns": {"年月": "date", "当月": "retail_sales"},
    },
    ("china", "industrial_production"): {
        "name": "中国工业增加值同比",
        "fetch": lambda: ak.macro_china_industrial_production_yoy(),
        "columns": {"年月": "date", "工业增加值": "industrial_production"},
    },
    ("china", "fixed_asset_investment"): {
        "name": "中国固定资产投资",
        "fetch": lambda: ak.macro_china_fixed_asset_investment(),
        "columns": {"年月": "date", "累计同比": "fixed_asset_investment"},
    },
    ("china", "interest_rate"): {
        "name": "中国贷款市场报价利率(LPR)",
        "fetch": lambda: ak.macro_china_lpr(),
        "columns": {"日期": "date", "1年期": "lpr_1y", "5年期": "lpr_5y"},
    },

    # === 美国宏观 ===
    ("usa", "cpi"): {
        "name": "美国消费者物价指数(CPI)",
        "fetch": lambda: ak.macro_usa_cpi_monthly(),
        "columns": {"月份": "date", "cpi": "cpi"},
    },
    ("usa", "unemployment"): {
        "name": "美国失业率",
        "fetch": lambda: ak.macro_usa_unemployment_rate(),
        "columns": {"月份": "date", "失业率": "unemployment"},
    },
    ("usa", "nonfarm"): {
        "name": "美国非农就业人数",
        "fetch": lambda: ak.macro_usa_non_farm_payrolls(),
        "columns": {"月份": "date", "非农就业人数": "nonfarm"},
    },
    ("usa", "gdp"): {
        "name": "美国国内生产总值(GDP)",
        "fetch": lambda: ak.macro_usa_gdp_monthly(),
        "columns": {"日期": "date", "gdp": "gdp"},
    },
    ("usa", "interest_rate"): {
        "name": "美国联邦基金利率",
        "fetch": lambda: ak.macro_usa_federal_funds_rate(),
        "columns": {"日期": "date", "利率": "fed_rate"},
    },
    ("usa", "consumer_confidence"): {
        "name": "美国消费者信心指数",
        "fetch": lambda: ak.macro_usa_michigan_consumer_sentiment(),
        "columns": {"日期": "date", "消费者信心指数": "confidence"},
    },
    ("usa", "housing_starts"): {
        "name": "美国新屋开工总数",
        "fetch": lambda: ak.macro_usa_house_price_index(),
        "columns": {"月份": "date", "新屋开工": "housing_starts"},
    },
}


def list_indicators():
    """列出所有支持的指标"""
    print("\n" + "=" * 70)
    print("支持的宏观经济指标")
    print("=" * 70)

    current_country = None
    for (country, indicator), info in MACRO_INDICATORS.items():
        if country != current_country:
            current_country = country
            country_name = "中国" if country == "china" else "美国" if country == "usa" else "全球"
            print(f"\n【{country_name}】")
            print("-" * 50)

        print(f"  --indicator {indicator:<25} {info['name']}")

    print("\n" + "=" * 70)
    print(f"总计 {len(MACRO_INDICATORS)} 个指标")
    print("用法: python fetch_macro_data.py --indicator <指标> --country <china|usa>")
    print("=" * 70 + "\n")


def fetch_and_print(indicator: str, country: str, start_date: str = None, end_date: str = None):
    """
    获取宏观数据并打印输出

    Args:
        indicator: 指标类型
        country: 国家
        start_date: 开始日期 (YYYY-MM-DD)
        end_date: 结束日期 (YYYY-MM-DD)
    """
    key = (country, indicator)
    if key not in MACRO_INDICATORS:
        print(f"错误: 不支持的指标类型 '{indicator}' (country={country})")
        print("可用指标:")
        for (c, i), info in MACRO_INDICATORS.items():
            if c == country:
                print(f"  - {i}: {info['name']}")
        return

    info = MACRO_INDICATORS[key]
    print(f"正在获取 {info['name']}...")

    try:
        df = info["fetch"]()
    except Exception as e:
        print(f"获取数据失败: {e}")
        return

    if df is None or df.empty:
        print("未获取到数据")
        return

    # 重命名列
    col_map = info["columns"]
    df = df.rename(columns=col_map)

    # 筛选日期范围
    if start_date or end_date:
        date_col = None
        for col in ["date", "year", "年月", "月份", "日期"]:
            if col in df.columns:
                date_col = col
                break

        if date_col and date_col in df.columns:
            try:
                df[date_col] = df[date_col].astype(str)
                if start_date:
                    df = df[df[date_col] >= start_date]
                if end_date:
                    df = df[df[date_col] <= end_date]
            except Exception:
                pass  # 日期格式不匹配，跳过筛选

    # 打印结果
    print("\n" + "=" * 70)
    print(f"{info['name']}")
    print("=" * 70)

    # 获取最新日期
    date_col = "date" if "date" in df.columns else (df.columns[0] if len(df.columns) > 0 else None)
    value_cols = [c for c in df.columns if c != date_col] if date_col else df.columns

    if date_col:
        latest_date = df[date_col].iloc[-1]
        print(f"最新数据日期: {latest_date}")
        print(f"数据条数: {len(df)}")

    print("-" * 70)

    # 打印表头
    headers = [date_col] if date_col else []
    headers.extend(value_cols[:5])  # 最多打印5列
    print("  ".join(f"{h:>15}" for h in headers))
    print("-" * 70)

    # 打印最近12条数据
    display_df = df.tail(12)
    for _, row in display_df.iterrows():
        values = []
        for h in headers:
            val = row[h]
            try:
                values.append(f"{float(val):>15.2f}")
            except (ValueError, TypeError):
                values.append(f"{str(val):>15}")
        print("  ".join(values))

    print("=" * 70)

    # 打印统计信息
    if len(value_cols) > 0:
        main_col = value_cols[0]
        try:
            numeric = pd.to_numeric(df[main_col], errors="coerce").dropna()
            if len(numeric) > 0:
                print(f"\n统计信息 ({main_col}):")
                print(f"  最新值: {numeric.iloc[-1]:.2f}")
                print(f"  平均值: {numeric.mean():.2f}")
                print(f"  最小值: {numeric.min():.2f}")
                print(f"  最大值: {numeric.max():.2f}")
        except Exception:
            pass

    # 可选: 保存为CSV
    # output_file = f"macro_{country}_{indicator}_{datetime.now().strftime('%Y%m%d')}.csv"
    # df.to_csv(output_file, index=False, encoding="utf-8-sig")
    # print(f"\n数据已保存到: {output_file}")


def main():
    import pandas as pd

    parser = argparse.ArgumentParser(
        description="获取中国/全球宏观经济指标数据",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python fetch_macro_data.py --list                          # 列出所有指标
  python fetch_macro_data.py --indicator cpi --country china # 获取中国CPI
  python fetch_macro_data.py --indicator pmi --country china --start 2024-01-01 --end 2026-01-01
  python fetch_macro_data.py --indicator cpi --country usa
        """
    )
    parser.add_argument("--indicator", type=str, help="指标类型 (cpi/ppi/gdp/pmi/m2等)")
    parser.add_argument("--country", type=str, default="china", choices=["china", "usa", "global"],
                        help="国家/地区 (默认china)")
    parser.add_argument("--start", type=str, help="开始日期 (YYYY-MM-DD)")
    parser.add_argument("--end", type=str, help="结束日期 (YYYY-MM-DD)")
    parser.add_argument("--list", action="store_true", help="列出所有支持的指标")

    args = parser.parse_args()

    if args.list:
        list_indicators()
        return

    if not args.indicator:
        print("错误: 请指定 --indicator 参数，或使用 --list 查看可用指标")
        parser.print_help()
        return

    fetch_and_print(args.indicator, args.country, args.start, args.end)


if __name__ == "__main__":
    main()
