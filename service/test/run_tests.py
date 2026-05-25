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
            "passwd": "密码",
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

    def __init__(self, config_path: Path, temp_config: Optional[Dict] = None, port: int = 19107):
        self.config_path = config_path
        self.temp_config = temp_config  # 临时配置（交互输入）
        self.port = port
        self.process: Optional[subprocess.Popen] = None
        self.base_url = f"https://localhost:{port}/v0"
        self.effective_config_path: Optional[Path] = None

    def _prepare_config(self) -> Path:
        """准备配置文件（如果需要临时覆盖）"""
        if not self.temp_config:
            return self.config_path

        # 写入临时配置到 tmp 文件
        tmp_path = CONFIGS_DIR / f".temp_{self.config_path.name}"
        with open(tmp_path, "w") as f:
            json.dump(self.temp_config, f, indent=4)
        return tmp_path

    def _cleanup_config(self):
        """清理临时配置文件"""
        if self.effective_config_path and self.effective_config_path.name.startswith(".temp_"):
            try:
                self.effective_config_path.unlink()
            except Exception:
                pass

    def is_running(self) -> bool:
        """检查服务是否运行"""
        try:
            url = f"{self.base_url}/server/status"
            req = urllib.request.Request(url, method="GET")
            import ssl
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            resp = urllib.request.urlopen(req, context=ctx, timeout=3)
            return resp.status == 200
        except Exception:
            return False

    def start(self, timeout: int = 30) -> bool:
        """启动服务并等待就绪"""
        if self.is_running():
            cprint(f"[服务] 已在运行 (port={self.port})", Colors.GREEN)
            return True

        if not SERVICE_BIN.exists():
            cprint(f"[错误] 服务程序不存在: {SERVICE_BIN}", Colors.RED)
            cprint(f"  请先编译: cd service/build && cmake --build .", Colors.YELLOW)
            return False

        self.effective_config_path = self._prepare_config()

        cprint(f"[服务] 启动中...", Colors.CYAN)
        cprint(f"  配置: {self.effective_config_path.name}", Colors.DIM)

        try:
            self.process = subprocess.Popen(
                [str(SERVICE_BIN), str(self.effective_config_path)],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                cwd=str(BUILD_DIR),
            )
        except Exception as e:
            cprint(f"[错误] 启动失败: {e}", Colors.RED)
            self._cleanup_config()
            return False

        # 等待服务就绪
        cprint(f"[服务] 等待就绪 (timeout={timeout}s)...", Colors.DIM)
        for i in range(timeout):
            if self.is_running():
                cprint(f"[服务] 已就绪 ✓", Colors.GREEN)
                return True
            time.sleep(1)

        cprint(f"[错误] 服务启动超时 ({timeout}s)", Colors.RED)
        self.stop()
        return False

    def stop(self):
        """停止服务"""
        # 先尝试优雅退出
        try:
            url = f"{self.base_url}/exit"
            req = urllib.request.Request(url, method="POST")
            import ssl
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            urllib.request.urlopen(req, context=ctx, timeout=3)
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
        manager = ServiceManager(config_file, temp_overrides if temp_overrides else None)
        if not manager.start():
            results["status"] = "ERROR"
            return results
    else:
        manager = None
        cprint(f"\n[手动模式] 请确保服务已启动", Colors.YELLOW)

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
