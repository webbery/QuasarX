import requests
import pytest
import os
import glob
from pathlib import Path

# 抑制 SSL 警告（本地测试不需要证书验证）
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

BASE_URL = "https://localhost:19107"
VERIFY_SSL = False


@pytest.fixture(scope="session", autouse=True)
def cleanup_test_scripts():
    """
    测试结束后清理 build/scripts/ 目录下 test_* 开头的策略文件。
    避免测试产生的策略脚本残留。
    """
    yield  # 先运行所有测试

    # 测试结束后执行清理
    scripts_dir = Path(__file__).parent.parent / "build" / "scripts"
    if scripts_dir.exists():
        removed = []
        for f in scripts_dir.glob("test_*"):
            f.unlink()
            removed.append(f.name)
        if removed:
            print(f"\n[Cleanup] Removed {len(removed)} test scripts: {', '.join(removed)}")


@pytest.fixture(scope="session", autouse=True)
def upload_test_data(auth_api, is_backtest):
    """
    回测模式下，扫描 script/ 目录提取策略引用的标的，只导入这些标的的数据到 DuckDB。
    测试结束后自动清理导入的数据。
    """
    if not is_backtest:
        yield
        return

    import re

    data_dir = Path(__file__).parent.parent / "build" / "data"
    script_dir = Path(__file__).parent.parent / "test" / "script"
    token = auth_api.token
    headers = {"Authorization": token}

    # === 从策略脚本中提取引用的标的 ===
    stock_symbols = set()
    etf_symbols = set()

    def _extract_symbols(directory):
        """从目录中所有 JSON 文件提取标的，自动区分股票/ETF"""
        stocks, etfs = set(), set()
        if not directory.exists():
            return stocks, etfs
        for script_file in directory.glob("*.json"):
            content = script_file.read_text()
            for sym in re.findall(r'(?:sh|sz|bj)\.\d{6}', content):
                code = sym.split('.')[1]
                prefix = int(code[:3])
                if (510 <= prefix <= 519) or prefix == 588 or prefix == 159:
                    etfs.add(sym)
                else:
                    stocks.add(sym)
        return stocks, etfs

    # script/ 目录（集成测试策略）
    s, e = _extract_symbols(script_dir)
    stock_symbols |= s
    etf_symbols |= e

    # metric_test_data/ 目录（指标验证测试策略）
    metric_dir = Path(__file__).parent / "testcases" / "metric_test_data"
    s, e = _extract_symbols(metric_dir)
    stock_symbols |= s
    etf_symbols |= e

    if not stock_symbols and not etf_symbols:
        print("\n[upload_test_data] 未找到策略引用的标的，跳过")
        yield
        return

    # 记录已导入的 (table, symbol) 用于清理
    imported_keys = []
    success_count = 0
    fail_count = 0

    def _ensure_turnover(lines):
        """如果 CSV 只有 6 列，补齐 turnover 列"""
        result = []
        for i, line in enumerate(lines):
            if i == 0:
                if 'turnover' not in line.lower():
                    result.append(line + ',turnover')
                else:
                    result.append(line)
            else:
                cols = line.split(',')
                if len(cols) < 7:
                    result.append(line + ',0')
                else:
                    result.append(line)
        return result

    def _import_csv(csv_path, table, symbol, adj):
        with open(csv_path, 'r') as f:
            lines = f.read().strip().split('\n')
        if len(lines) < 2:
            return False
        lines = _ensure_turnover(lines)
        try:
            resp = requests.post(f"{BASE_URL}/v0/quote/data", json={
                "action": "import",
                "table": table,
                "symbol": symbol,
                "adj": adj,
                "data": lines
            }, headers=headers, verify=VERIFY_SSL)
            return resp.status_code == 200
        except Exception:
            return False

    # === 导入股票数据 (日线) ===
    hfq_dir = data_dir / "A_hfq"
    for symbol in sorted(stock_symbols):
        csv_path = hfq_dir / f"{symbol}.csv"
        if not csv_path.exists():
            print(f"  [SKIP] 股票 {symbol}: CSV 不存在")
            continue
        ok1 = _import_csv(csv_path, "stock_1d", symbol, "hfq")
        ok2 = _import_csv(csv_path, "stock_1d", symbol, "none")
        if ok1 and ok2:
            imported_keys.append(("stock_1d", symbol))
            success_count += 1
        else:
            fail_count += 1

    # === 导入 ETF 数据 (按频率) ===
    etf_hfq_dir = data_dir / "etf_hfq"
    for symbol in sorted(etf_symbols):
        if not etf_hfq_dir.exists():
            print(f"  [SKIP] ETF {symbol}: etf_hfq 目录不存在")
            continue
        imported = False
        for freq_dir in etf_hfq_dir.iterdir():
            if not freq_dir.is_dir():
                continue
            csv_path = freq_dir / f"{symbol}.csv"
            if not csv_path.exists():
                continue
            table = f"etf_{freq_dir.name}"
            ok1 = _import_csv(csv_path, table, symbol, "hfq")
            ok2 = _import_csv(csv_path, table, symbol, "none")
            if ok1 and ok2:
                imported_keys.append((table, symbol))
                imported = True
        if imported:
            success_count += 1
        else:
            print(f"  [SKIP] ETF {symbol}: 无可用 CSV")
            fail_count += 1

    print(f"\n[upload_test_data] 导入完成: 成功 {success_count}, 失败 {fail_count}")
    print(f"  股票: {sorted(stock_symbols)}")
    print(f"  ETF:  {sorted(etf_symbols)}")

    yield

    # === 清理：删除导入的测试数据 ===
    if imported_keys:
        print(f"\n[cleanup] 清理 {len(imported_keys)} 个标的的测试数据...")
        cleanup_count = 0
        for table, symbol in imported_keys:
            try:
                requests.delete(f"{BASE_URL}/v0/quote", params={
                    "table": table, "symbol": symbol
                }, headers=headers, verify=VERIFY_SSL)
                cleanup_count += 1
            except Exception:
                pass
        print(f"[cleanup] 已清理 {cleanup_count} 个标的")


class AuthAPI:
    """模拟认证API"""
    def __init__(self):
        self._mode = None
        self._token = None

    def login(self):
        """模拟登录请求"""
        params = {"name": 'admin', 'pwd': 'admin'}
        response = requests.post(f"{BASE_URL}/v0/user/login", json=params, verify=VERIFY_SSL)
        data = response.json()
        assert isinstance(data, object)
        assert 'mode' in data
        token = data['tk']
        self._token = token
        self._mode = data['mode']
        assert len(token) > 0
        return token

    @property
    def token(self):
        """认证 token 字符串"""
        return self._token

    @property
    def mode(self):
        """当前服务运行模式，如 'backtest' / 'simulation' / 'realtime'"""
        return self._mode

    def is_backtest_mode(self):
        """判断是否为回测模式"""
        return self._mode and 'backtest' in str(self._mode).lower()


@pytest.fixture(scope="session")
def auth_api():
    """提供 AuthAPI 实例，包含 mode 信息"""
    api = AuthAPI()
    api.login()
    return api


@pytest.fixture(scope="session")
def auth_token(auth_api):
    """提供认证 token 字符串（保持向后兼容）"""
    return auth_api.token


@pytest.fixture(scope="session")
def is_backtest(auth_api):
    """判断当前是否为回测模式"""
    return auth_api.is_backtest_mode()
    