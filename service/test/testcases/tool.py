

import json
import pytest
import requests
import urllib3
from pathlib import Path

urllib3.disable_warnings()

# --------------------------
# 路径常量
# --------------------------

def find_service_root() -> Path:
    """查找服务根目录

    支持多种环境（按优先级尝试）：
    1. 本地有构建: 查找 build/QuantService → 返回 service/
    2. CI 二进制在 repo root: 查找 QuantService → 返回 repo root
    3. 本地有源码: 查找 CMakeLists.txt → 返回 service/
    4. 通用标记: 查找 config.template.json
    5. CI 只有数据: 查找 data/ 目录
    """
    markers = [
        ("build/QuantService", False),  # (相对路径, 是否为目录)
        ("QuantService", False),
        ("CMakeLists.txt", False),
        ("config.template.json", False),
        ("data", True),
    ]
    
    for marker, is_dir in markers:
        current = Path(__file__).resolve().parent
        for _ in range(10):
            target = current / marker
            if target.exists() and (not is_dir or target.is_dir()):
                return current
            current = current.parent
    
    raise RuntimeError(f"Cannot find service root from {Path(__file__).resolve()}")

SERVICE_ROOT = find_service_root()


def _resolve_data_dir() -> Path:
    """定位服务运行时的数据根目录（db_path 解析后的绝对路径）

    服务运行目录有三种布局：
    - CI (find_service_root 找到 repo root): 二进制在 SERVICE_ROOT → 数据在 SERVICE_ROOT/data
    - CI (find_service_root 找到 service/): 二进制在 SERVICE_ROOT 的上一级 → 数据在 parent/data
    - 本地: service/build/ 下有 QuantService 二进制 → 数据在 service/build/data
    - 兜底: SERVICE_ROOT/data
    """
    # 二进制在 SERVICE_ROOT
    if (SERVICE_ROOT / "QuantService").exists():
        return SERVICE_ROOT / "data"
    # 二进制在 SERVICE_ROOT 的上一级（CI 布局：find_service_root 找到 service/，但二进制在 repo root）
    if (SERVICE_ROOT.parent / "QuantService").exists():
        return SERVICE_ROOT.parent / "data"
    # 本地开发：二进制在 service/build/
    build_dir = SERVICE_ROOT / "build"
    if (build_dir / "QuantService").exists():
        return build_dir / "data"
    return SERVICE_ROOT / "data"


_DATA_DIR = _resolve_data_dir()
DEBUG_DIR = _DATA_DIR / "data" / "debug"
CSV_DATA_DIR = _DATA_DIR / "A_hfq"

# --------------------------
# Debug CSV 读取
# --------------------------

def read_debug_csv(strategy_id: str, label: str) -> "pd.DataFrame":
    """读取 DebugNode 生成的 CSV
    
    格式:
      Line 1: datetime,col1,col2,... (列名)
      Lines 2+: 数据
    
    Args:
        strategy_id: 策略 ID
        label: DebugNode 的 label
    
    Returns:
        DataFrame，列名为原始列名
    """
    import pandas as pd
    
    csv_path = DEBUG_DIR / strategy_id / f"{label}.csv"
    assert csv_path.exists(), f"Debug CSV not found: {csv_path}"
    
    # 读取第一行获取列名
    with open(csv_path) as f:
        first_line = f.readline().strip()
    columns = first_line.split(",")
    
    # 读取数据
    df = pd.read_csv(csv_path, skiprows=1, header=None, names=columns)
    return df


# --------------------------
# 原有代码
# --------------------------

# 默认 BASE_URL（硬编码回退）
_DEFAULT_BASE_URL = "https://localhost:19107/v0"

def _load_base_url_from_config():
    """尝试从 test_config.json 读取 base_url，失败则返回默认值"""
    try:
        test_dir = Path(__file__).parent
        config_path = test_dir / "test_config.json"
        if config_path.exists():
            with open(config_path, "r", encoding="utf-8") as f:
                cfg = json.load(f)
                return cfg.get("base_url", _DEFAULT_BASE_URL)
    except Exception:
        pass
    return _DEFAULT_BASE_URL

BASE_URL = _load_base_url_from_config()
VERIFY_SSL = False

# --------------------------
# 测试工具函数
# --------------------------
def check_response(response, expected_status=200):
    """验证响应状态码和基本结构"""
    assert response.status_code == expected_status
    if expected_status == 200:
        if response.content == b'null':
            return None

        if len(response.content) > 0:
            assert isinstance(response.json(), (dict, list))
            return response.json()

    return None


# --------------------------
# 回测 helper
# --------------------------

METRIC_DATA_DIR = Path(__file__).parent / "metric_test_data"


def load_strategy(name: str) -> str:
    """从 metric_test_data/ 加载策略 JSON 文件内容"""
    path = METRIC_DATA_DIR / name
    if not path.exists():
        return json.dumps({"status": "error", "error": f"策略文件不存在: {path}"})
    with open(path) as f:
        return f.read()


def run_backtest(strategy_str: str, headers: dict, validate: bool = True) -> dict:
    """提交回测并返回结果。失败返回 {"status": "error", "error": "..."}

    Args:
        strategy_str: 策略 JSON 字符串
        headers: 请求头（含 Authorization）
        validate: 是否校验策略图（节点测试传 False 跳过校验）
    """
    try:
        resp = requests.post(
            f"{BASE_URL}/backtest",
            json={"script": strategy_str, "validate": validate},
            headers=headers,
            verify=VERIFY_SSL,
            timeout=300,
        )
        if resp.status_code != 200:
            return {"status": "error", "error": f"HTTP {resp.status_code}: {resp.text[:300]}"}
        return resp.json()
    except Exception as e:
        return {"status": "error", "error": str(e)}



# if __name__ == "__main__":
#     check_login()