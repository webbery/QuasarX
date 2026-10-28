

import json
import pytest
import requests
import time
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


def get_auth_token() -> str:
    """获取认证 token（通用登录接口）"""
    resp = requests.post(
        f"{BASE_URL}/user/login",
        json={"name": "admin", "pwd": "admin"},
        verify=VERIFY_SSL,
    )
    resp.raise_for_status()
    return resp.json()["tk"]


# --------------------------
# 策略管理 API
# --------------------------

def api_load_strategy(token: str, name: str, script: dict) -> dict:
    """加载策略（只初始化，不运行）
    
    Args:
        token: 认证 token
        name: 策略名称
        script: 策略 JSON dict
    
    Returns:
        API 响应 dict
    """
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "load", "name": name, "script": script},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()


def api_run_strategy(token: str, name: str) -> dict:
    """设置策略为运行模式
    
    Args:
        token: 认证 token
        name: 策略名称
    
    Returns:
        API 响应 dict
    """
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"mode": 1, "name": name},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()


def api_stop_strategy(token: str, name: str) -> dict:
    """暂停策略
    
    Args:
        token: 认证 token
        name: 策略名称
    
    Returns:
        API 响应 dict
    """
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"mode": 2, "name": name},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=30,
    )
    resp.raise_for_status()
    return resp.json()


# --------------------------
# 日终策略执行 API
# --------------------------

def api_simulate_bar(token: str, bar: dict) -> dict:
    """推送 bar 数据触发日终策略执行
    
    Args:
        token: 认证 token
        bar: bar 数据 dict，包含 symbol, datetime, open, high, low, close, volume
    
    Returns:
        API 响应 dict。失败时返回 {"status": "error", "error": ...}
    """
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy/simulate/bar",
        json=bar,
        headers=headers,
        verify=VERIFY_SSL,
        timeout=30,
    )
    if resp.ok:
        return resp.json()
    return {
        "status": "error",
        "error": f"HTTP {resp.status_code}: {resp.text[:200]}",
    }


def api_get_decisions(token: str, strategy: str = None, date: str = None) -> list:
    """获取决策列表
    
    Args:
        token: 认证 token
        strategy: 策略名称（可选，不传则返回所有策略的决策）
        date: 日期字符串，格式 YYYY-MM-DD（可选）
    
    Returns:
        决策列表
    """
    headers = {"Authorization": token}
    params = {}
    if strategy:
        params["strategy"] = strategy
    if date:
        params["date"] = date
    resp = requests.get(
        f"{BASE_URL}/trade/decisions",
        params=params,
        headers=headers,
        verify=VERIFY_SSL,
        timeout=10,
    )
    if resp.ok:
        return resp.json() or []
    return []


def wait_for_decisions(token: str, strategy: str, date: str, timeout: int = 15) -> list:
    """等待决策结果
    
    Args:
        token: 认证 token
        strategy: 策略名称
        date: 日期字符串
        timeout: 超时时间（秒）
    
    Returns:
        决策列表，超时返回空列表
    """
    deadline = time.time() + timeout
    while time.time() < deadline:
        decisions = api_get_decisions(token, strategy, date)
        if decisions and len(decisions) > 0:
            return decisions
        time.sleep(0.5)
    return []


def api_clear_decisions(token: str, date: str = None) -> int:
    """清除指定日期的决策
    
    Args:
        token: 认证 token
        date: 日期字符串（可选，不传则清除所有决策）
    
    Returns:
        删除的决策数量
    """
    headers = {"Authorization": token}
    params = {}
    if date:
        params["date"] = date
    resp = requests.delete(
        f"{BASE_URL}/trade/decisions",
        params=params,
        headers=headers,
        verify=VERIFY_SSL,
        timeout=10,
    )
    if resp.ok:
        return resp.json().get("deleted", 0)
    return 0


def api_cleanup_strategy(token: str, name: str):
    """清理测试策略（停止并删除）
    
    Args:
        token: 认证 token
        name: 策略名称
    """
    headers = {"Authorization": token}
    try:
        # 先停止
        requests.post(
            f"{BASE_URL}/strategy",
            json={"mode": 2, "name": name},
            headers=headers,
            verify=VERIFY_SSL,
            timeout=5,
        )
        # 再删除
        requests.delete(
            f"{BASE_URL}/strategy",
            json={"name": name},
            headers=headers,
            verify=VERIFY_SSL,
            timeout=5,
        )
    except Exception:
        pass


def api_order_with_decision_id(
    token: str, symbol: str, decision_id: int,
    quantity: int, price: float, direct: int = 0,
) -> requests.Response:
    """POST /v0/trade/order 关联 decisionId，返回原始 Response
    
    Args:
        token: 认证 token
        symbol: 标的代码
        decision_id: 决策 ID
        quantity: 数量
        price: 价格
        direct: 方向（0=买入，1=卖出）
    
    Returns:
        requests.Response 对象
    """
    headers = {"Authorization": token}
    body = {
        "symbol": symbol,
        "direct": direct,
        "type": 1,  # 限价单
        "quantity": quantity,
        "prices": price,
        "kind": 0,
        "timeType": 0,
        "decisionId": decision_id,
    }
    return requests.post(
        f"{BASE_URL}/trade/order",
        json=body,
        headers=headers,
        verify=VERIFY_SSL,
        timeout=10,
    )


def to_api_symbol(symbol: str) -> str:
    """转换标的代码格式：sz.900001 → 900001.SZ
    
    Args:
        symbol: 内部格式标的代码，如 sz.000001, sh.600000
    
    Returns:
        API 格式标的代码，如 000001.SZ, 600000.SH
    """
    if "." not in symbol:
        return symbol
    exchange, code = symbol.split(".", 1)
    exchange_upper = exchange.upper()
    return f"{code}.{exchange_upper}"


def from_api_symbol(symbol: str) -> str:
    """转换标的代码格式：900001.SZ → sz.900001（to_api_symbol 的逆运算）
    
    Args:
        symbol: API 格式标的代码，如 000001.SZ
    
    Returns:
        内部格式标的代码，如 sz.000001
    """
    if "." not in symbol:
        return symbol
    code, exchange = symbol.rsplit(".", 1)
    exchange_lower = exchange.lower()
    return f"{exchange_lower}.{code}"


def today_str() -> str:
    """获取今日日期字符串，格式 YYYY-MM-DD"""
    return time.strftime("%Y-%m-%d")


# --------------------------
# 资金与持仓 API
# --------------------------

def api_get_credit_state(token: str, strategy_name: str) -> dict:
    """获取策略信用状态（两融）

    Args:
        token: 认证 token
        strategy_name: 策略名称

    Returns:
        信用状态 dict，包含 borrowed_cash, borrowed_shares, short_avg_price, financed_amount
    """
    headers = {"Authorization": token}
    resp = requests.post(
        f"{BASE_URL}/strategy",
        json={"action": "credit_state", "name": strategy_name},
        headers=headers,
        verify=VERIFY_SSL,
        timeout=10,
    )
    resp.raise_for_status()
    return resp.json()


def api_get_latest_bar(token: str, symbol: str, start=None, end=None,
                       date: str = None) -> dict:
    """获取最新 bar 数据

    端点 /stocks/history 用 ``id`` 参数（不是 ``symbol``），外部格式为
    ``<code>.<EXCHANGE>``（如 900001.SZ），服务端再转回内部格式。

    Args:
        token: 认证 token
        symbol: 内部格式标的代码，如 sz.900001
        start: 起始时间，日期字符串 "YYYY-MM-DD" 或 Unix 秒；None = 不限
        end:   结束时间，同上；None = 不限
        date:  覆盖返回 dict 的 datetime 字段（可选）

    Returns:
        bar 数据 dict，datetime 已转成 "YYYY-MM-DD HH:MM:SS"

    Raises:
        ValueError: 无测试数据
    """
    headers = {"Authorization": token}
    api_symbol = to_api_symbol(symbol)
    params = {"id": api_symbol, "type": "1d"}
    if start is not None:
        params["start"] = start
    if end is not None:
        params["end"] = end

    try:
        resp = requests.get(
            f"{BASE_URL}/stocks/history",
            params=params,
            headers=headers,
            verify=VERIFY_SSL,
            timeout=10,
        )
    except requests.RequestException as e:
        raise ValueError(f"Cannot get bar data for {api_symbol}: {e}")

    if not resp.ok:
        raise ValueError(f"Cannot get bar data for {api_symbol}: HTTP {resp.status_code}")

    try:
        data = resp.json()
    except Exception as e:
        raise ValueError(f"Cannot parse bar data for {api_symbol}: {e}")

    if not isinstance(data, list) or not data:
        raise ValueError(f"Empty bar data for {api_symbol}")

    last = data[-1]
    # datetime 是 Unix 时间戳（数字），需转成 "YYYY-MM-DD HH:MM:SS" 字符串
    raw_dt = last.get("datetime", "")
    try:
        dt_str = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(int(raw_dt)))
    except (ValueError, TypeError):
        dt_str = str(raw_dt)

    return {
        "symbol": symbol,
        "open": float(last.get("open", 0)),
        "high": float(last.get("high", 0)),
        "low": float(last.get("low", 0)),
        "close": float(last.get("close", 0)),
        "volume": int(float(last.get("volume", 0))),
        "datetime": date or dt_str,
    }


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