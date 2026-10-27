

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

    服务运行目录有四种布局：
    - CI (find_service_root 找到 repo root): 二进制在 SERVICE_ROOT → 数据在 SERVICE_ROOT/data
    - CI (find_service_root 找到 service/): 二进制在 SERVICE_ROOT 的上一级 → 数据在 parent/data
    - 本地 Linux: service/build/ 下有 QuantService 二进制 → 数据在 service/build/data
    - 本地 Windows: service/winbuild/ 下有 QuantService.exe → 数据在 service/winbuild/data
    - 兜底: SERVICE_ROOT/data
    """
    # 二进制在 SERVICE_ROOT
    if (SERVICE_ROOT / "QuantService").exists():
        return SERVICE_ROOT / "data"
    # 二进制在 SERVICE_ROOT 的上一级（CI 布局：find_service_root 找到 service/，但二进制在 repo root）
    if (SERVICE_ROOT.parent / "QuantService").exists():
        return SERVICE_ROOT.parent / "data"
    # 本地 Linux 开发：二进制在 service/build/
    build_dir = SERVICE_ROOT / "build"
    if (build_dir / "QuantService").exists():
        return build_dir / "data"
    # 本地 Windows 开发：二进制在 service/winbuild/{Debug,Release}/
    winbuild_dir = SERVICE_ROOT / "winbuild"
    if (winbuild_dir / "data").is_dir():
        return winbuild_dir / "data"
    return SERVICE_ROOT / "data"


_DATA_DIR = _resolve_data_dir()
DEBUG_DIR = _DATA_DIR / "debug"
CSV_DATA_DIR = _DATA_DIR / "A_hfq"


def _resolve_scripts_dir() -> Path:
    """定位服务运行时的 scripts 目录（C++ SCRIPTS_DIR="scripts" 的绝对路径）

    与 _resolve_data_dir 同理，取决于服务 cwd：
      - CI：QuantService 在 SERVICE_ROOT → SERVICE_ROOT/scripts
      - 本地 Linux：QuantService 在 SERVICE_ROOT/build/ → SERVICE_ROOT/build/scripts
      - 本地 Windows：QuantService 在 SERVICE_ROOT/winbuild/ → SERVICE_ROOT/winbuild/scripts
    """
    if (SERVICE_ROOT / "QuantService").exists():
        return SERVICE_ROOT / "scripts"
    if (SERVICE_ROOT.parent / "QuantService").exists():
        return SERVICE_ROOT.parent / "scripts"
    build_dir = SERVICE_ROOT / "build"
    if (build_dir / "QuantService").exists():
        return build_dir / "scripts"
    winbuild_dir = SERVICE_ROOT / "winbuild"
    if (winbuild_dir / "scripts").is_dir():
        return winbuild_dir / "scripts"
    return SERVICE_ROOT / "scripts"


SCRIPTS_DIR = _resolve_scripts_dir()

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


def reclaim_capital(token: str, strategy: str = None) -> None:
    """归还策略资金（测试隔离用）。strategy=None 归还全部，否则归还指定策略。"""
    body = {"action": "reclaim_all"} if strategy is None else {"action": "reclaim", "name": strategy}
    requests.post(
        f"{BASE_URL}/strategy",
        json=body,
        headers={"Authorization": token},
        verify=VERIFY_SSL,
        timeout=30,
    )


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
# 风控夹具由 node_test_data/generate_protection_data.py 生成，生成脚本与产物一起进版本库
PROTECTION_DATA_DIR = Path(__file__).parent / "node_test_data" / "protection"

# 策略夹具查找顺序：指标夹具（generate_test_data.py 产出）→ 风控夹具
STRATEGY_DIRS = (METRIC_DATA_DIR, PROTECTION_DATA_DIR)


def load_strategy(name: str) -> str:
    """加载策略 JSON 文件内容。

    找不到文件直接抛 FileNotFoundError。早先这里返回一段
    ``{"status": "error", ...}`` 的合法 JSON，服务端会把它当成"空策略"解析
    （name/id 为空、nodes/edges 为 0），回测跑完没有 protection_events，
    测试报成"风控没触发"，排查方向被完全带偏 —— 实际是文件没进版本库。
    """
    for base in STRATEGY_DIRS:
        path = base / name
        if path.exists():
            return path.read_text(encoding="utf-8")
    searched = ", ".join(str(base) for base in STRATEGY_DIRS)
    raise FileNotFoundError(f"策略文件不存在: {name}（已查找: {searched}）")


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


def run_backtest_graph(strategy: dict, headers: dict, validate: bool = False,
                       timeout: int = 1800) -> dict:
    """提交策略图（dict）回测，失败直接抛 AssertionError

    与 run_backtest 的区别：接收 dict 而非 JSON 字符串，且失败时抛异常而非返回
    error 字典——节点测试需要「回测没跑起来」就是测试失败，不能被静默跳过。

    提取自 test_signal_logic.py 与 test_node_unit.py 各自的 _run_backtest，
    两者签名不同（dict / Path），此处统一为 dict 入口。
    """
    resp = requests.post(
        f"{BASE_URL}/backtest",
        json={"script": json.dumps(strategy), "validate": validate},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=timeout,
    )
    assert resp.status_code == 200, \
        f"Backtest failed: HTTP {resp.status_code}: {resp.text[:300]}"
    return resp.json()



# if __name__ == "__main__":
#     check_login()