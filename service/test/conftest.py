import requests
import pytest
import os
import glob
import time
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
    import sys
    sys.path.insert(0, str(Path(__file__).parent / "testcases"))
    from tool import SERVICE_ROOT, _resolve_data_dir

    data_dir = _resolve_data_dir()
    # 测试合成数据（如 sz.900007）在 build/data/，服务运行数据在 winbuild/data/
    # 导入时优先从 build/data/ 查找 CSV，回退到服务数据目录
    _test_data_dir = SERVICE_ROOT / "build" / "data"
    script_dir = SERVICE_ROOT / "test" / "script"
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
            content = script_file.read_text(encoding="utf-8")
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

    # node_test_data/ 目录（节点单元/集成测试策略）
    node_dir = Path(__file__).parent / "testcases" / "node_test_data"
    s, e = _extract_symbols(node_dir)
    stock_symbols |= s
    etf_symbols |= e

    # ai_test_data/ 目录（AI 模型训练测试策略）
    ai_dir = Path(__file__).parent / "testcases" / "ai_test_data"
    s, e = _extract_symbols(ai_dir)
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

    def _import_csv(org_path, hfq_path, table, symbol):
        """导入行情数据：org 写入原始列，hfq 写入 adj_* 列"""
        with open(hfq_path, 'r') as f:
            hfq_lines = f.read().strip().split('\n')
        if len(hfq_lines) < 2:
            return False
        hfq_lines = _ensure_turnover(hfq_lines)

        # org 数据：如果存在则读取，否则回退到 hfq（兼容无 org 数据的场景）
        if org_path and org_path.exists():
            with open(org_path, 'r') as f:
                org_lines = f.read().strip().split('\n')
            org_lines = _ensure_turnover(org_lines)
        else:
            org_lines = hfq_lines

        try:
            resp = requests.post(f"{BASE_URL}/v0/quote/data", json={
                "action": "import",
                "table": table,
                "symbol": symbol,
                "data": org_lines,
                "data_hfq": hfq_lines,
            }, headers=headers, verify=VERIFY_SSL)
            return resp.status_code == 200
        except Exception:
            return False

    def _resolve_csv(filename, *search_dirs):
        """在多个数据目录中查找同名 CSV，返回第一个存在的路径"""
        for d in search_dirs:
            p = d / filename
            if p.exists():
                return p
        return search_dirs[0] / filename  # 兜底返回第一个路径

    # 数据目录搜索顺序：build/data/（测试合成数据） > 服务数据目录（生产数据）
    _csv_search_dirs = [_test_data_dir, data_dir]

    def _freq_names(root_name):
        """收集 {root_name}/{freq}/ 下出现过的频率名（30m/5m/...），跨数据目录合并"""
        names = set()
        for d in _csv_search_dirs:
            root = d / root_name
            if root.is_dir():
                names.update(p.name for p in root.iterdir() if p.is_dir())
        return sorted(names)

    def _freq_csv(root_name, freq, symbol):
        """查找 {root_name}/{freq}/ 下的标的 CSV，找不到返回 None

        兼容两种命名：合成数据用 {symbol}.csv（sz.900007.csv），
        download_etf_bs.py 的产物用 CODE.EXCHANGE.csv（000423.SZ.csv）。
        """
        code, _, market = symbol.partition(".")
        names = [symbol] + ([f"{code}.{market.upper()}"] if code else [])
        for d in _csv_search_dirs:
            for n in names:
                p = d / root_name / freq / f"{n}.csv"
                if p.exists():
                    return p
        return None

    # === 导入股票数据 (日线 + 各分钟频) ===
    stock_freqs = _freq_names("stock_hfq")
    if stock_freqs:
        print(f"[upload_test_data] 检测到股票分钟频数据: {stock_freqs}")
    for symbol in sorted(stock_symbols):
        hfq_path = _resolve_csv(f"A_hfq/{symbol}.csv", *_csv_search_dirs)
        org_path = _resolve_csv(f"AStock/{symbol}.csv", *_csv_search_dirs)
        if not hfq_path.exists():
            print(f"  [SKIP] 股票 {symbol}: 日线 hfq CSV 不存在")
        else:
            ok = _import_csv(org_path, hfq_path, "stock_1d", symbol)
            if ok:
                imported_keys.append(("stock_1d", symbol))
                success_count += 1
            else:
                fail_count += 1

        # 分钟频：stock_hfq|stock_org/{freq}/{symbol}.csv → stock_{freq}
        # 目录约定与 download_etf_bs.py --asset-type stock 的输出同构
        for freq in stock_freqs:
            freq_hfq = _freq_csv("stock_hfq", freq, symbol)
            if freq_hfq is None:
                continue
            freq_org = _freq_csv("stock_org", freq, symbol)
            table = f"stock_{freq}"
            if _import_csv(freq_org, freq_hfq, table, symbol):
                imported_keys.append((table, symbol))
                success_count += 1
            else:
                fail_count += 1

    # === 导入 ETF 数据 (按频率) ===
    etf_hfq_dir = data_dir / "etf_hfq"
    etf_org_dir = data_dir / "etf_org"
    for symbol in sorted(etf_symbols):
        if not etf_hfq_dir.exists():
            print(f"  [SKIP] ETF {symbol}: etf_hfq 目录不存在")
            continue
        imported = False
        for freq_dir in etf_hfq_dir.iterdir():
            if not freq_dir.is_dir():
                continue
            hfq_path = freq_dir / f"{symbol}.csv"
            if not hfq_path.exists():
                continue
            org_path = etf_org_dir / freq_dir.name / f"{symbol}.csv"
            table = f"etf_{freq_dir.name}"
            ok = _import_csv(org_path, hfq_path, table, symbol)
            if ok:
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

    # === 导入分红数据 ===
    dividend_dir = data_dir / "dividend"
    dividend_imported = False
    if dividend_dir.exists() and any(dividend_dir.glob("*.csv")):
        try:
            resp = requests.post(f"{BASE_URL}/v0/dividend", json={
                "action": "import",
                "dividend_dir": str(dividend_dir),
            }, headers=headers, verify=VERIFY_SSL)
            if resp.status_code == 200:
                dividend_imported = True
                rows = resp.json().get("imported_rows", 0)
                print(f"  分红: 导入 {rows} 条 (from {dividend_dir})")
            else:
                print(f"  分红: 导入失败 HTTP {resp.status_code}")
        except Exception as e:
            print(f"  分红: 导入异常 {e}")

    yield

    # === 清理：删除导入的测试数据 ===
    # 使用 DROP TABLE 而非逐行 DELETE，避免反复 DELETE/INSERT 触发 DuckDB ART 索引损坏
    if imported_keys:
        print(f"\n[cleanup] 清理 {len(imported_keys)} 个标的的测试数据...")
        cleanup_count = 0
        dropped_tables = set()
        for table, symbol in imported_keys:
            if table in dropped_tables:
                continue
            try:
                requests.delete(f"{BASE_URL}/v0/quote", params={
                    "table": table
                }, headers=headers, verify=VERIFY_SSL)
                dropped_tables.add(table)
                cleanup_count += 1
            except Exception:
                pass
        print(f"[cleanup] 已清理 {cleanup_count} 张表: {sorted(dropped_tables)}")


class AuthAPI:
    """模拟认证API"""
    def __init__(self):
        self._mode = None
        self._token = None

    def login(self, retries=10, delay=2):
        """模拟登录请求（带重试，等待服务完全就绪）"""
        params = {"name": 'admin', 'pwd': 'admin'}
        last_err = None
        for i in range(retries):
            try:
                response = requests.post(f"{BASE_URL}/v0/user/login", json=params, verify=VERIFY_SSL, timeout=5)
                data = response.json()
                assert isinstance(data, object)
                assert 'mode' in data
                token = data['tk']
                self._token = token
                self._mode = data['mode']
                assert len(token) > 0
                return token
            except Exception as e:
                last_err = e
                if i < retries - 1:
                    time.sleep(delay)
        raise ConnectionError(f"登录失败（重试 {retries} 次）: {last_err}")

    @property
    def token(self):
        """认证 token 字符串"""
        return self._token

    @property
    def mode(self):
        """当前服务运行模式，如 'backtest' / 'simulation' / 'realtime'"""
        return self._mode

    def is_backtest_mode(self):
        """判断是否为回测模式（含 Simulation/stock_hist_sim）"""
        if not self._mode:
            return False
        m = self._mode.lower()
        return 'backtest' in m or 'simulation' in m or 'sim' in m


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


@pytest.fixture
def headers(auth_token):
    """带认证的请求头"""
    return {"Authorization": auth_token} if auth_token else {}


@pytest.fixture(scope="session")
def is_backtest(auth_api):
    """判断当前是否为回测模式"""
    return auth_api.is_backtest_mode()


def cancel_ml_session(auth_token, session_id):
    """
    取消一个活跃的 ML 会话。
    调用 /ml?action=cancel 接口终止后台 Python 进程。
    """
    if not auth_token or not session_id:
        return False
    try:
        resp = requests.post(
            f"{BASE_URL}/ml",
            json={"action": "cancel", "session_id": session_id},
            headers={"Authorization": auth_token},
            verify=VERIFY_SSL,
            timeout=5
        )
        return resp.status_code == 200
    except Exception:
        return False


def _wait_for_active_session(auth_token, max_wait=60):
    """
    等待当前活跃的 ML 会话完成。
    通过尝试发起一个请求，如果收到 409，则从响应中获取 session_id 并等待。
    返回 (success: bool, session_id: str or None, timed_out: bool)
    """
    if not auth_token:
        return True, None, False

    headers = {"Authorization": auth_token}
    start_time = time.time()
    last_session_id = None

    while time.time() - start_time < max_wait:
        # 尝试发起一个无害的请求来检查是否有活跃会话
        # 使用一个空的 train 请求，handleTrain 会先检查活跃会话，再验证参数
        # 如果收到 409，说明有活跃会话
        try:
            resp = requests.post(
                f"{BASE_URL}/ml",
                json={"action": "train", "script": ""},
                headers=headers,
                verify=VERIFY_SSL,
                timeout=5
            )
            if resp.status_code == 409:
                # 有活跃会话，从响应中获取 session_id
                data = resp.json()
                session_id = data.get("running_session_id")
                last_session_id = session_id
                if session_id:
                    # 等待该会话完成
                    wait_start = time.time()
                    while time.time() - wait_start < max_wait:
                        try:
                            status_resp = requests.get(
                                f"{BASE_URL}/ml",
                                params={"action": "train_status", "session_id": session_id},
                                headers=headers,
                                verify=VERIFY_SSL,
                                timeout=5
                            )
                            if status_resp.status_code == 200:
                                status_data = status_resp.json()
                                if status_data.get("status") in ("done", "error"):
                                    break
                        except Exception:
                            pass
                        time.sleep(0.5)
                    else:
                        # 内层超时，返回 timed_out
                        return False, session_id, True
                    continue
            # 如果没有收到 409，说明没有活跃会话
            return True, None, False
        except Exception:
            return True, None, False
    
    # 外层超时
    return False, last_session_id, True


@pytest.fixture(autouse=True)
def wait_for_ml_sessions(auth_token):
    """
    每个测试开始前，等待所有活跃的 ML 会话完成。
    防止前一个测试的后台任务（如 optimize）影响当前测试。
    如果超时，skip 当前测试而不是让它撞墙。
    """
    success, session_id, timed_out = _wait_for_active_session(auth_token, max_wait=60)
    if timed_out:
        pytest.skip(f"ML session {session_id} still running after 60s, skipping to avoid 409 conflict")
    yield
    