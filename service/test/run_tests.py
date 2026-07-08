#!/usr/bin/env python3
"""
自动化测试运行器 — 支持三种 exchange 模式，服务自动启停、交互配置、彩色输出、结果汇总。

使用方式：
    python run_tests.py                         # 运行所有可用模式
    python run_tests.py --mode stock_hist_sim   # 只运行回测模式
    python run_tests.py --mode tickflow         # 只运行实时行情模式
    python run_tests.py --mode hx               # 只运行华鑫实盘模式
    python run_tests.py --list                  # 列出所有模式及测试项
    python run_tests.py --tests strategy        # 运行包含 strategy 的测试
    python run_tests.py --no-service            # 不自启服务（手动启动）
    python run_tests.py --dry-run               # 只打印计划，不执行
"""

import json
import os
import sys
import time
import copy
import subprocess
import argparse
import urllib.request
import urllib.error
from pathlib import Path
from datetime import datetime
from typing import Optional, Dict, List

# ==================== 颜色输出 ====================

class Colors:
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    BLUE = "\033[94m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    RESET = "\033[0m"
    BG_GREEN = "\033[42m"
    BG_RED = "\033[41m"
    BG_YELLOW = "\033[43m"


def cprint(text, color=None, bold=False):
    prefix = ""
    if color:
        prefix += color
    if bold:
        prefix += Colors.BOLD
    print(f"{prefix}{text}{Colors.RESET}")


def banner(text):
    width = 70
    print()
    cprint(f"{'═' * width}", Colors.CYAN)
    cprint(f"  {text}", Colors.CYAN, bold=True)
    cprint(f"{'═' * width}", Colors.CYAN)


def section(text):
    cprint(f"\n{'─' * 50}", Colors.DIM)
    cprint(f"  {text}", Colors.YELLOW, bold=True)
    cprint(f"{'─' * 50}", Colors.DIM)


# ==================== 路径配置 ====================

SCRIPT_DIR = Path(__file__).parent.resolve()
CONFIGS_DIR = SCRIPT_DIR / "configs"
TESTCASES_DIR = SCRIPT_DIR / "testcases"
BUILD_DIR = SCRIPT_DIR.parent / "build"
SERVICE_BIN = BUILD_DIR / "QuantService"
MODES_FILE = SCRIPT_DIR / "test_modes.json"
TEMPLATE_FILE = SCRIPT_DIR.parent / "config.template.json"

# ==================== 模式加载 ====================

def load_modes() -> Dict:
    """加载 test_modes.json"""
    if not MODES_FILE.exists():
        cprint(f"[错误] 模式配置文件不存在: {MODES_FILE}", Colors.RED)
        sys.exit(1)
    with open(MODES_FILE) as f:
        return json.load(f)["modes"]


# ==================== 交互配置 ====================

def prompt_credentials(fields: List[str]) -> Dict[str, str]:
    """交互式提示输入凭证"""
    result = {}
    print()
    for field in fields:
        label = {
            "username": "用户名",
            "passwd": "密码",
            "key": "API Key",
        }.get(field, field)

        if field in ("passwd", "password", "key"):
            import getpass
            value = getpass.getpass(f"  {label}: ")
        else:
            value = input(f"  {label}: ").strip()

        result[field] = value
    return result


def check_mode_config(mode_name: str, mode_info: Dict) -> tuple:
    """
    检查模式配置是否完整。
    返回 (is_ready, temp_overrides)
    temp_overrides 是临时配置字典，用于启动服务时覆盖
    """
    config_file = CONFIGS_DIR / mode_info["config"]
    if not config_file.exists():
        return False, {}

    with open(config_file) as f:
        config_data = json.load(f)

    # 检查是否需要交互输入
    required_fields = mode_info.get("requires_input", [])
    if not required_fields:
        return True, {}

    # 检查现有配置是否已填写
    exchanges = config_data.get("exchange", [])
    if exchanges:
        ex = exchanges[0]
        all_filled = all(ex.get(f, "").strip() for f in required_fields)
        if all_filled:
            return True, {}

    # 需要交互输入
    cprint(f"\n[⚠️] {mode_info['label']} — 配置不完整", Colors.YELLOW)
    cprint(f"  请输入账号信息（仅本次有效，不保存到文件）:", Colors.DIM)

    credentials = prompt_credentials(required_fields)

    if not all(credentials.values()):
        cprint(f"[跳过] 输入为空，跳过此模式", Colors.YELLOW)
        return False, {}

    # 构建临时覆盖配置
    temp_overrides = copy.deepcopy(config_data)
    if temp_overrides.get("exchange"):
        for field, value in credentials.items():
            temp_overrides["exchange"][0][field] = value

    return True, temp_overrides


# ==================== 服务管理 ====================

class ServiceManager:
    """管理服务进程的生命周期"""

    def __init__(self, config_path: Path, mode_name: str = "", temp_config: Optional[Dict] = None, port: int = 19107):
        self.config_path = config_path
        self.mode_name = mode_name
        self.temp_config = temp_config  # 临时配置（交互输入）
        self.port = port
        self.process: Optional[subprocess.Popen] = None
        self.effective_config_path: Optional[Path] = None
        self.log_file: Optional[Path] = None
        self._scheme = self._detect_scheme()

    def _detect_scheme(self) -> str:
        """根据 build 目录下是否存在 TLS 证书判断 HTTP/HTTPS"""
        if (BUILD_DIR / "server.crt").exists() and (BUILD_DIR / "server.key").exists():
            return "https"
        return "http"

    @property
    def base_url(self) -> str:
        return f"{self._scheme}://localhost:{self.port}/v0"

    def _ssl_context(self):
        import ssl
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        return ctx

    @staticmethod
    def _deep_merge(base: dict, override: dict) -> dict:
        """递归合并，override 中的非空值覆盖 base"""
        result = base.copy()
        for key, value in override.items():
            if value is None or value == "" or value == []:
                continue
            if key in result and isinstance(result[key], dict) and isinstance(value, dict):
                result[key] = ServiceManager._deep_merge(result[key], value)
            else:
                result[key] = value
        return result

    def _generate_config(self) -> Path:
        """从模板 + 模式配置生成最终 config.json，写入 build/ 目录"""
        # 读取模板
        with open(TEMPLATE_FILE) as f:
            config = json.load(f)

        # 读取模式配置
        with open(self.config_path) as f:
            mode_config = json.load(f)

        # 按模式过滤 exchange：只保留该模式 server.default.exchange 引用的
        needed_names = set(mode_config.get("server", {}).get("default", {}).get("exchange", []))
        if needed_names:
            config["exchange"] = [
                ex for ex in config.get("exchange", [])
                if ex.get("name") in needed_names
            ]

        # 合并模式配置的实际值（server/broker/risk/etf 等）
        config = self._deep_merge(config, mode_config)

        # 交互输入的临时覆盖（如 hx 模式的 username/passwd）
        if self.temp_config:
            config = self._deep_merge(config, self.temp_config)

        # 写入 build/config.json（服务工作目录）
        output = BUILD_DIR / "config.json"
        with open(output, "w") as f:
            json.dump(config, f, indent=4)

        cprint(f"  生成配置: {output.name} (exchange: {[ex.get('name') for ex in config.get('exchange', [])]})", Colors.DIM)
        return output

    def _cleanup_config(self):
        """清理生成的配置文件"""
        pass  # build/config.json 保留供调试查看

    def is_running(self) -> bool:
        """检查服务是否运行 — 收到任何 HTTP 响应（含 401 未鉴权）均视为已启动"""
        import http.client
        try:
            url = f"{self.base_url}/server/status"
            req = urllib.request.Request(url, method="GET")
            urllib.request.urlopen(req, context=self._ssl_context(), timeout=3)
            return True  # 200 或其他成功状态
        except urllib.error.HTTPError as e:
            # 401/403 等说明服务已在运行，只是鉴权未通过
            return e.code in (401, 403)
        except (urllib.error.URLError, http.client.RemoteDisconnected, ConnectionError, OSError):
            return False
        except Exception:
            return False

    def _check_alive(self) -> bool:
        """检查进程是否还存活"""
        if self.process is None:
            return False
        return self.process.poll() is None

    def _dump_diagnostics(self):
        """输出启动失败时的诊断信息"""
        # 进程状态
        if self.process is not None:
            rc = self.process.poll()
            if rc is not None:
                cprint(f"[诊断] 进程已退出，returncode={rc}", Colors.RED)
            else:
                cprint(f"[诊断] 进程仍在运行 (pid={self.process.pid})，但健康检查未通过", Colors.YELLOW)
                cprint(f"  可能原因: 协议不匹配 (尝试 {self._scheme})、端口冲突、初始化阻塞", Colors.DIM)

        # 日志尾部
        if self.log_file and self.log_file.exists():
            try:
                lines = self.log_file.read_text(errors="replace").splitlines()
                tail = lines[-50:] if len(lines) > 50 else lines
                if tail:
                    cprint(f"\n[诊断] 服务日志尾部 ({self.log_file.name}, 最后 {len(tail)} 行):", Colors.CYAN)
                    for line in tail:
                        cprint(f"  {line}", Colors.DIM)
            except Exception:
                cprint(f"[诊断] 无法读取日志文件: {self.log_file}", Colors.DIM)

        # spdlog
        spdlog = BUILD_DIR / "logs" / "monthly_log.txt"
        if spdlog.exists():
            try:
                lines = spdlog.read_text(errors="replace").splitlines()
                tail = lines[-20:] if len(lines) > 20 else lines
                if tail:
                    cprint(f"\n[诊断] spdlog 尾部 (最后 {len(tail)} 行):", Colors.CYAN)
                    for line in tail:
                        cprint(f"  {line}", Colors.DIM)
            except Exception:
                pass

    def start(self, timeout: int = 30) -> bool:
        """启动服务并等待就绪"""
        if self.is_running():
            cprint(f"[服务] 已在运行 (port={self.port}, {self._scheme})", Colors.GREEN)
            return True

        if not SERVICE_BIN.exists():
            cprint(f"[错误] 服务程序不存在: {SERVICE_BIN}", Colors.RED)
            cprint(f"  请先编译: cd service/build && cmake --build .", Colors.YELLOW)
            return False

        self.effective_config_path = self._generate_config()

        # 日志输出到文件
        self.log_file = BUILD_DIR / "service_run_test.log"
        log_fd = open(self.log_file, "w")

        cprint(f"[服务] 启动中...", Colors.CYAN)
        cprint(f"  配置: {self.effective_config_path.name}", Colors.DIM)
        cprint(f"  协议: {self._scheme} (TLS 证书 {'存在' if self._scheme == 'https' else '不存在'})", Colors.DIM)
        cprint(f"  日志: {self.log_file}", Colors.DIM)

        try:
            self.process = subprocess.Popen(
                [str(SERVICE_BIN), str(self.effective_config_path)],
                stdout=log_fd,
                stderr=subprocess.STDOUT,
                cwd=str(BUILD_DIR),
            )
        except Exception as e:
            cprint(f"[错误] 启动失败: {e}", Colors.RED)
            log_fd.close()
            self._cleanup_config()
            return False

        # 等待服务就绪
        cprint(f"[服务] 等待就绪 (timeout={timeout}s)...", Colors.DIM)
        for i in range(timeout):
            # 提前检测进程崩溃
            if not self._check_alive():
                rc = self.process.returncode
                cprint(f"[错误] 服务进程已退出 (returncode={rc})", Colors.RED)
                log_fd.close()
                self._dump_diagnostics()
                self.process = None
                self._cleanup_config()
                return False

            if self.is_running():
                cprint(f"[服务] 已就绪 ✓ ({i+1}s)", Colors.GREEN)
                log_fd.close()
                return True
            time.sleep(1)

        cprint(f"[错误] 服务启动超时 ({timeout}s)", Colors.RED)
        log_fd.close()
        self._dump_diagnostics()
        self.stop()
        return False

    def _login(self) -> Optional[str]:
        """login 获取 JWT token，失败返回 None"""
        try:
            # 从配置中读取用户名密码
            config_path = self.effective_config_path or self.config_path
            with open(config_path) as f:
                cfg = json.load(f)
            user = cfg.get("server", {}).get("user", "admin")
            passwd = cfg.get("server", {}).get("passwd", "admin")

            url = f"{self.base_url}/user/login"
            data = json.dumps({"name": user, "pwd": passwd}).encode()
            req = urllib.request.Request(url, data=data, method="POST",
                                        headers={"Content-Type": "application/json"})
            resp = urllib.request.urlopen(req, context=self._ssl_context(), timeout=5)
            body = json.loads(resp.read())
            return body.get("tk")
        except Exception:
            return None

    def stop(self):
        """停止服务"""
        # 先 login 获取 token，再携带 token 发送退出请求
        token = self._login()
        if token:
            try:
                url = f"{self.base_url}/exit"
                req = urllib.request.Request(url, method="POST",
                                            headers={"Authorization": token})
                urllib.request.urlopen(req, context=self._ssl_context(), timeout=3)
                cprint(f"[服务] 已发送退出请求", Colors.DIM)
            except Exception:
                pass

        if self.process:
            try:
                self.process.terminate()
                self.process.wait(timeout=5)
            except Exception:
                self.process.kill()
                cprint(f"[服务] 强制终止", Colors.YELLOW)

            cprint(f"[服务] 已停止", Colors.DIM)
            self.process = None

        self._cleanup_config()

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.stop()


# ==================== 测试执行 ====================

def run_test_module(test_name: str, specific_tests: list = None) -> dict:
    """运行单个测试模块，返回结果

    支持子目录路径格式，如 "hx/test_hx_trade" 会解析为 testcases/hx/test_hx_trade.py
    """
    # 支持子目录路径：将 "/" 替换为 os.sep
    test_file = TESTCASES_DIR / f"{test_name.replace('/', os.sep)}.py"

    if not test_file.exists():
        return {"name": test_name, "status": "SKIP", "reason": "文件不存在"}

    cmd = ["pytest", str(test_file), "-v", "--tb=short", "--color=yes"]

    if specific_tests:
        for t in specific_tests:
            cmd.extend(["-k", t])

    try:
        result = subprocess.run(
            cmd,
            cwd=str(SCRIPT_DIR),
            capture_output=False,
            timeout=300,  # 5 分钟超时
        )

        if result.returncode == 0:
            return {"name": test_name, "status": "PASS"}
        elif result.returncode == 5:
            return {"name": test_name, "status": "SKIP", "reason": "无匹配测试"}
        else:
            return {"name": test_name, "status": "FAIL"}
    except subprocess.TimeoutExpired:
        return {"name": test_name, "status": "FAIL", "reason": "超时 (300s)"}
    except Exception as e:
        return {"name": test_name, "status": "FAIL", "reason": str(e)}


def run_mode(mode_name: str, mode_info: Dict, args) -> dict:
    """运行指定模式的所有测试"""
    results = {
        "mode": mode_name,
        "label": mode_info["label"],
        "tests": [],
        "skipped": [],
        "failed": [],
        "passed": [],
    }

    # 检查配置
    is_ready, temp_overrides = check_mode_config(mode_name, mode_info)
    if not is_ready:
        results["status"] = "SKIPPED"
        return results

    # 筛选测试
    tests_to_run = mode_info["tests"]
    if args.tests:
        tests_to_run = [t for t in tests_to_run if any(a in t for a in args.tests)]

    if not tests_to_run:
        cprint(f"\n[警告] {mode_info['label']} 无匹配测试", Colors.YELLOW)
        results["status"] = "EMPTY"
        return results

    banner(f"模式: {mode_info['label']} ({mode_name})")
    cprint(f"  {mode_info['description']}", Colors.DIM)
    cprint(f"  测试数量: {len(tests_to_run)}", Colors.DIM)

    # 启动服务
    if not args.no_service:
        config_file = CONFIGS_DIR / mode_info["config"]
        manager = ServiceManager(config_file, mode_name=mode_name, temp_config=temp_overrides if temp_overrides else None)
        if not manager.start():
            results["status"] = "ERROR"
            return results
    else:
        manager = None
        # 检查服务是否已在运行
        import http.client
        scheme = "https" if (BUILD_DIR / "server.crt").exists() and (BUILD_DIR / "server.key").exists() else "http"
        check_url = f"{scheme}://localhost:{args.port}/v0/server/status"
        ctx = ssl.create_default_context()
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
        try:
            urllib.request.urlopen(urllib.request.Request(check_url, method="GET"), context=ctx, timeout=3)
            alive = True
        except urllib.error.HTTPError as e:
            alive = e.code in (401, 403)  # 有响应即说明服务在运行
        except Exception:
            alive = False

        if alive:
            cprint(f"\n[手动模式] 服务已运行 ({scheme}://localhost:{args.port})", Colors.GREEN)
        else:
            cprint(f"\n[错误] 服务未运行 ({check_url})，请先启动 QuantService", Colors.RED)
            results["status"] = "ERROR"
            return results

    try:
        # 执行测试
        for i, test_name in enumerate(tests_to_run, 1):
            section(f"[{i}/{len(tests_to_run)}] {test_name}")

            result = run_test_module(test_name, args.tests)
            results["tests"].append(result)

            status_icon = {
                "PASS": f"{Colors.BG_GREEN} PASS {Colors.RESET}",
                "FAIL": f"{Colors.BG_RED} FAIL {Colors.RESET}",
                "SKIP": f"{Colors.BG_YELLOW} SKIP {Colors.RESET}",
            }.get(result["status"], result["status"])

            reason = f" — {result.get('reason', '')}" if result.get("reason") else ""
            cprint(f"  {test_name}: {status_icon}{reason}")

            if result["status"] == "PASS":
                results["passed"].append(test_name)
            elif result["status"] == "FAIL":
                results["failed"].append(test_name)
            else:
                results["skipped"].append(test_name)

    finally:
        if manager:
            manager.stop()

    # 汇总
    total = len(results["tests"])
    passed = len(results["passed"])
    failed = len(results["failed"])
    skipped = len(results["skipped"])

    banner(f"结果: {mode_info['label']}")
    cprint(f"  总计: {total}  通过: {Colors.GREEN}{passed}{Colors.RESET}  "
           f"失败: {Colors.RED if failed else Colors.GREEN}{failed}{Colors.RESET}  "
           f"跳过: {Colors.YELLOW}{skipped}{Colors.RESET}")

    results["status"] = "PASS" if not failed else "FAIL"
    return results


# ==================== CLI ====================

def list_modes(modes: Dict):
    """列出所有模式"""
    banner("可用测试模式")

    for name, mode in modes.items():
        available = CONFIGS_DIR / mode["config"]
        exists = "✅" if available.exists() else "❌"
        cprint(f"\n  {exists} {name} — {mode['label']}", bold=True)
        cprint(f"     {mode.get('description', '')}", Colors.DIM)
        cprint(f"     配置: configs/{mode['config']}", Colors.DIM)

        requires = mode.get("requires_input", [])
        if requires:
            cprint(f"     需要输入: {', '.join(requires)}", Colors.YELLOW)

        cprint(f"     测试项 ({len(mode['tests'])}):", Colors.DIM)
        for t in mode["tests"]:
            cprint(f"       • {t}", Colors.DIM)

    cprint(f"\n💡 自定义测试项: 编辑 test_modes.json 中对应模式的 tests 数组", Colors.CYAN)


def print_summary(all_results: list):
    """打印所有模式的汇总"""
    banner("总汇总")

    total_pass = 0
    total_fail = 0
    total_skip = 0

    for r in all_results:
        status = r.get("status", "UNKNOWN")
        status_icon = {
            "PASS": f"{Colors.BG_GREEN} ✓ {Colors.RESET}",
            "FAIL": f"{Colors.BG_RED} ✗ {Colors.RESET}",
            "SKIPPED": f"{Colors.BG_YELLOW} ⊘ {Colors.RESET}",
            "ERROR": f"{Colors.BG_RED} ✗ {Colors.RESET}",
            "EMPTY": f"{Colors.DIM} - {Colors.RESET}",
        }.get(status, "?")

        label = r.get("label", r["mode"])
        passed = len(r.get("passed", []))
        failed = len(r.get("failed", []))
        skipped = len(r.get("skipped", []))

        cprint(f"  {status_icon} {label}", bold=True)
        detail_parts = []
        detail_parts.append(f"通过: {passed}")
        if failed:
            detail_parts.append(f"{Colors.RED}失败: {failed}{Colors.RESET}")
        else:
            detail_parts.append(f"失败: {failed}")
        detail_parts.append(f"跳过: {skipped}")
        cprint(f"     {'  '.join(detail_parts)}", Colors.DIM)

        total_pass += passed
        total_fail += failed
        total_skip += skipped

    cprint(f"\n{'─' * 50}", Colors.DIM)
    cprint(f"  总计: 通过={total_pass}  失败={total_fail}  跳过={total_skip}", bold=True)

    if total_fail > 0:
        cprint(f"\n  失败的测试:", Colors.RED, bold=True)
        for r in all_results:
            for t in r.get("failed", []):
                cprint(f"    • [{r['label']}] {t}", Colors.RED)

    return total_fail == 0


def main():
    parser = argparse.ArgumentParser(
        description="QuasarX 自动化测试运行器",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python run_tests.py                           运行所有可用模式
  python run_tests.py --mode stock_hist_sim     只运行回测模式
  python run_tests.py --mode tickflow           只运行实时行情模式
  python run_tests.py --mode hx                 只运行华鑫实盘模式
  python run_tests.py --list                    列出所有模式
  python run_tests.py --tests strategy          运行包含 strategy 的测试
  python run_tests.py --tests strategy shadow   运行多个匹配项
  python run_tests.py --no-service              不自启服务
  python run_tests.py --dry-run                 只打印计划
        """,
    )

    parser.add_argument(
        "--mode",
        choices=["stock_hist_sim", "tickflow", "hx", "all"],
        default="all",
        help="测试模式 (默认: all)",
    )
    parser.add_argument(
        "--tests",
        nargs="*",
        help="指定测试名称（支持部分匹配）",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="列出所有模式及测试项",
    )
    parser.add_argument(
        "--no-service",
        action="store_true",
        help="不自动启停服务（需手动启动）",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="只打印执行计划，不实际运行",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=19107,
        help="服务端口 (默认: 19107)",
    )

    args = parser.parse_args()
    modes = load_modes()

    if args.list:
        list_modes(modes)
        return

    # 确定要运行的模式
    if args.mode == "all":
        mode_names = list(modes.keys())
    else:
        if args.mode not in modes:
            cprint(f"[错误] 未知模式: {args.mode}", Colors.RED)
            sys.exit(1)
        mode_names = [args.mode]

    # Dry run
    if args.dry_run:
        banner("执行计划 (dry-run)")
        for name in mode_names:
            mode = modes[name]
            tests = mode["tests"]
            if args.tests:
                tests = [t for t in tests if any(a in t for a in args.tests)]
            requires = mode.get("requires_input", [])
            req_str = f" (需要输入: {', '.join(requires)})" if requires else ""
            cprint(f"  {name} ({mode['label']}){req_str}: {len(tests)} 个测试", bold=True)
            for t in tests:
                cprint(f"    • {t}", Colors.DIM)
        return

    # 执行
    start_time = datetime.now()
    cprint(f"\n开始时间: {start_time.strftime('%Y-%m-%d %H:%M:%S')}", Colors.DIM)

    all_results = []
    for name in mode_names:
        result = run_mode(name, modes[name], args)
        all_results.append(result)

    # 总汇总
    success = print_summary(all_results)

    elapsed = (datetime.now() - start_time).total_seconds()
    cprint(f"\n耗时: {elapsed:.1f}s", Colors.DIM)

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
