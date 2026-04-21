"""
爬取中国货币网 SHIBOR 数据
数据来源: https://www.chinamoney.com.cn/chinese/bkshibor

功能:
  1. 获取最新 SHIBOR 数据
  2. 获取历史 SHIBOR 数据
  3. 保存为 CSV/JSON 格式
  4. 统计利率变化

用法:
  python fetch_shibor.py                  # 获取最新数据
  python fetch_shibor.py --date 2026-04-20  # 获取指定日期数据
  python fetch_shibor.py --latest 10      # 获取最近10天数据
"""
import requests
import json
import csv
import argparse
from datetime import datetime, timedelta
from pathlib import Path

# SHIBOR 数据 API 接口
API_URL = "https://www.chinamoney.com.cn/ags/ms/cm-u-bk-shibor/ShiborHis"

# 请求头
HEADERS = {
    "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
    "Accept": "application/json",
    "Content-Type": "application/json",
    "Referer": "https://www.chinamoney.com.cn/chinese/bkshibor/",
    "Origin": "https://www.chinamoney.com.cn",
}

# 期限顺序
TERM_ORDER = ["隔夜", "1周", "2周", "1个月", "3个月", "6个月", "9个月", "1年"]
TERM_MAP = {
    "ON": "隔夜",
    "1W": "1周",
    "2W": "2周",
    "1M": "1个月",
    "3M": "3个月",
    "6M": "6个月",
    "9M": "9个月",
    "1Y": "1年"
}


def append_to_csv(data: list, filepath: Path):
    """
    追加数据到 shibor.csv
    
    CSV 格式:
    date,term,rate,change
    2026-04-21,隔夜,1.2200,0
    2026-04-21,1周,1.3310,0
    """
    filepath = Path(filepath)
    filepath.parent.mkdir(parents=True, exist_ok=True)
    
    # 检查文件是否存在，决定是否需要写表头
    need_header = not filepath.exists()
    
    # 如果文件存在，读取已有的 (date, term) 组合用于去重
    existing_keys = set()
    if filepath.exists():
        with open(filepath, 'r', encoding='utf-8-sig') as f:
            reader = csv.reader(f)
            next(reader, None)  # 跳过表头
            for row in reader:
                if len(row) >= 2:
                    existing_keys.add((row[0], row[1]))
    
    # 追加新数据
    new_count = 0
    with open(filepath, 'a', encoding='utf-8-sig', newline='') as f:
        writer = csv.writer(f)
        if need_header:
            writer.writerow(['date', 'term', 'rate', 'change'])
        
        for item in data:
            key = (item['日期'], item['期限'])
            if key not in existing_keys:
                writer.writerow([
                    item['日期'],
                    item['期限'],
                    item['利率(%)'],
                    0  # change 默认为 0，由前端计算
                ])
                existing_keys.add(key)
                new_count += 1
    
    return new_count


def fetch_shibor_by_date(date: str) -> list:
    """
    获取指定日期的 SHIBOR 数据
    
    Args:
        date: 日期，格式 YYYY-MM-DD
    
    Returns:
        列表，包含各期限利率
    """
    payload = {
        "lang": "CH",
        "endDate": date,
    }
    
    response = requests.post(API_URL, headers=HEADERS, json=payload, timeout=10)
    response.raise_for_status()
    data = response.json()
    
    if not data.get("records"):
        return []
    
    result = []
    for record in data["records"]:
        date_str = record.get("showDateCN", date)
        
        for term_en, term_cn in TERM_MAP.items():
            rate = record.get(term_en, "")
            if rate:
                result.append({
                    "日期": date_str,
                    "期限": term_cn,
                    "利率(%)": rate,
                })
    
    # 排序
    result.sort(key=lambda x: (x["日期"], TERM_ORDER.index(x["期限"]) if x["期限"] in TERM_ORDER else 99))
    return result


def fetch_shibor_history(days: int = 10, save_dir: str = "./data"):
    """
    获取最近 N 天的 SHIBOR 历史数据
    
    Args:
        days: 获取天数
        save_dir: 保存目录
    """
    save_path = Path(save_dir)
    save_path.mkdir(parents=True, exist_ok=True)
    
    all_data = []
    end_date = datetime.now()
    
    print(f"正在获取最近 {days} 天的 SHIBOR 数据...")
    
    for i in range(days):
        date = (end_date - timedelta(days=i)).strftime("%Y-%m-%d")
        try:
            data = fetch_shibor_by_date(date)
            if data:
                all_data.extend(data)
                print(f"  ✓ {date}: {len(data)//8} 个期限数据")
            else:
                print(f"  ✗ {date}: 无数据（非交易日）")
        except Exception as e:
            print(f"  ✗ {date}: 获取失败 - {e}")
    
    if not all_data:
        print("\n未获取到任何数据")
        return None
    
    print(f"\n成功获取 {len(all_data)} 条数据")
    
    # 保存为 JSON
    json_file = save_path / f"shibor_history_{days}d.json"
    with open(json_file, "w", encoding="utf-8") as f:
        json.dump(all_data, f, ensure_ascii=False, indent=2)
    print(f"数据已保存到: {json_file}")
    
    # 保存为 CSV
    csv_file = save_path / f"shibor_history_{days}d.csv"
    with open(csv_file, "w", encoding="utf-8-sig") as f:
        f.write("日期,期限,利率(%)\n")
        for item in all_data:
            f.write(f"{item['日期']},{item['期限']},{item['利率(%)']}\n")
    print(f"数据已保存到: {csv_file}")
    
    return all_data


def print_latest_table(data: list):
    """打印最新数据表格"""
    if not data:
        return
    
    # 获取最新日期
    latest_date = data[-1]["日期"]
    prev_date = None
    
    # 找上一个交易日
    for item in reversed(data):
        if item["日期"] != latest_date:
            prev_date = item["日期"]
            break
    
    # 按日期分组
    latest_rates = {}
    prev_rates = {}
    
    for item in data:
        if item["日期"] == latest_date:
            latest_rates[item["期限"]] = float(item["利率(%)"])
        elif item["日期"] == prev_date:
            prev_rates[item["期限"]] = float(item["利率(%)"])
    
    # 打印表格
    print("\n" + "=" * 60)
    print(f"SHIBOR 最新行情 ({latest_date})")
    print("=" * 60)
    print(f"{'期限':<8} {'利率(%)':<10} {'涨跌(BP)':<10}")
    print("-" * 60)
    
    for term in TERM_ORDER:
        if term in latest_rates:
            rate = latest_rates[term]
            change = ""
            if term in prev_rates:
                bp = (rate - prev_rates[term]) * 100
                sign = "+" if bp > 0 else ""
                change = f"{sign}{bp:.1f}"
            print(f"{term:<8} {rate:<10.4f} {change:<10}")
    
    print("=" * 60)


def main():
    parser = argparse.ArgumentParser(description="爬取中国货币网 SHIBOR 数据")
    parser.add_argument("--date", type=str, help="指定日期 (YYYY-MM-DD)")
    parser.add_argument("--latest", type=int, help="获取最近 N 天数据")
    parser.add_argument("--save-dir", type=str, default="./data", help="保存目录")
    parser.add_argument("--append-csv", action="store_true", help="追加写入到 shibor.csv")

    args = parser.parse_args()

    if args.latest:
        # 获取历史数据
        data = fetch_shibor_history(args.latest, args.save_dir)
        if data:
            print_latest_table(data)
    else:
        # 获取指定日期或今天
        date = args.date or datetime.now().strftime("%Y-%m-%d")
        data = fetch_shibor_by_date(date)

        if data:
            if args.append_csv:
                # 追加到 shibor.csv
                csv_file = Path(args.save_dir) / "shibor.csv"
                new_count = append_to_csv(data, csv_file)
                print(f"已追加 {new_count} 条数据到 {csv_file}")
            else:
                print(f"\n获取到 {len(data)} 条数据")

                # 打印表格
                print("\n" + "=" * 40)
                print(f"SHIBOR 数据 ({date})")
                print("=" * 40)
                print(f"{'期限':<8} {'利率(%)':<10}")
                print("-" * 40)
                for item in data:
                    print(f"{item['期限']:<8} {item['利率(%)']:<10}")
                print("=" * 40)

                # 保存数据
                save_path = Path(args.save_dir)
                save_path.mkdir(parents=True, exist_ok=True)

                csv_file = save_path / f"shibor_{date}.csv"
                with open(csv_file, "w", encoding="utf-8-sig") as f:
                    f.write("日期,期限,利率(%)\n")
                    for item in data:
                        f.write(f"{item['日期']},{item['期限']},{item['利率(%)']}\n")

                print(f"\n数据已保存到: {csv_file}")
        else:
            print(f"{date} 无 SHIBOR 数据（可能是非交易日）")


if __name__ == "__main__":
    main()
