

import json
from pathlib import Path

# --------------------------
# 路径常量
# --------------------------

def find_service_root() -> Path:
    """查找 service/ 目录
    
    支持多种环境（按优先级尝试）：
    1. CI 有构建产物: 查找 build/QuantService
    2. 本地有源码: 查找 CMakeLists.txt
    3. 通用标记: 查找 config.template.json
    4. CI 只有数据: 查找 data/ 目录
    """
    markers = [
        ("build/QuantService", False),  # (相对路径, 是否为目录)
        ("CMakeLists.txt", False),
        ("config.template.json", False),
        ("QuantService", False),  # (相对路径, 是否为目录)
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

    服务运行目录有两种布局：
    - 本地: service/build/ (build/ 下有 QuantService 二进制)
    - CI:   repo root/     (QuantService 在 SERVICE_ROOT 下)
    """
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



# if __name__ == "__main__":
#     check_login()