#!/usr/bin/env python3
"""
清理测试数据，恢复服务数据目录到测试前状态

用法：
  python testcases/cleanup_test_data.py
"""

import shutil
from pathlib import Path

# 服务数据目录
SERVICE_BUILD_DIR = Path(__file__).parent.parent / "build"
SERVICE_DATA_DIR = SERVICE_BUILD_DIR / "data"

# 需要恢复的文件
RESTORE_FILES = [
    ("symbol_market.csv.backup", "symbol_market.csv"),
]

# 需要删除的测试文件
TEST_SYMBOLS = [
    "sz.900001", "sz.900002", "sz.900003",
    "sz.900004", "sz.900005", "sz.900006",
]

# 需要删除的目录
TEST_DIRS = [
    "testcases/metric_test_data",
]


def main():
    print("清理测试数据...")
    
    # 1. 恢复 symbol_market.csv
    backup_path = SERVICE_DATA_DIR / "symbol_market.csv.backup"
    market_path = SERVICE_DATA_DIR / "symbol_market.csv"
    
    if backup_path.exists():
        shutil.copy2(backup_path, market_path)
        backup_path.unlink()
        print(f"  ✅ 恢复 {market_path}")
    else:
        print(f"  ⚠️  未找到备份文件: {backup_path}")
    
    # 2. 删除测试 CSV 文件
    for subdir in ["A_hfq", "AStock"]:
        dir_path = SERVICE_DATA_DIR / subdir
        if not dir_path.exists():
            continue
        for symbol in TEST_SYMBOLS:
            csv_file = dir_path / f"{symbol}.csv"
            if csv_file.exists():
                csv_file.unlink()
                print(f"  🗑️  删除 {csv_file}")
    
    # 3. 删除测试用例目录
    testcases_dir = Path(__file__).parent / "metric_test_data"
    if testcases_dir.exists():
        shutil.rmtree(testcases_dir)
        print(f"  🗑️  删除 {testcases_dir}")
    
    print("\n清理完成！")


if __name__ == "__main__":
    main()
