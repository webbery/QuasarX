#!/usr/bin/env python3
"""
统一测试入口 — 读取 test_config.json 控制模块启用/关闭，测试结束后发送退出请求。

使用方式：
    python main.py                  # 运行所有启用的模块
    python main.py order            # 只运行 order 模块
    python main.py order TestStockOrder::test_buy_order_basic  # 运行指定用例
    python main.py --list           # 列出所有模块及启用状态
"""

import json
import os
import sys
import subprocess
import requests
from pathlib import Path

# 项目根目录
TEST_DIR = Path(__file__).parent
CONFIG_PATH = TEST_DIR / "testcases" / "test_config.json"


def load_config():
    """加载测试配置"""
    if not CONFIG_PATH.exists():
        print(f"[WARN] 配置文件不存在: {CONFIG_PATH}，使用默认配置（运行全部）")
        return {"enabled_modules": [], "disabled_modules": [], "modules": {}, "exit_on_finish": True}
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        return json.load(f)


def discover_modules():
    """自动发现 testcases/ 目录下所有 test_*.py 文件"""
    testcases_dir = TEST_DIR / "testcases"
    modules = {}
    for f in sorted(testcases_dir.glob("test_*.py")):
        name = f.stem[len("test_"):]  # 去掉 test_ 前缀
        modules[name] = str(f)
    return modules


def resolve_enabled(config, all_modules, cli_args=None):
    """
    解析最终要运行的模块列表。

    优先级：CLI 参数 > disabled_modules > enabled_modules > 默认全部
    """
    # CLI 指定模块
    if cli_args:
        return [m for m in cli_args if m in all_modules]

    enabled = config.get("enabled_modules", [])
    disabled = config.get("disabled_modules", [])
    modules_cfg = config.get("modules", {})

    # 如果没有显式配置 enabled_modules，默认全部
    if not enabled:
        enabled = list(all_modules.keys())

    # 过滤 disabled（优先级更高）
    result = [m for m in enabled if m not in disabled]

    # 再检查 modules.<name>.enabled 字段
    final = []
    for m in result:
        mod_cfg = modules_cfg.get(m, {})
        if mod_cfg.get("enabled", True):  # 默认 true
            final.append(m)

    return final


def run_pytest(module_name, module_file, specific_test=None):
    """运行单个模块的 pytest"""
    cmd = ["pytest", module_file, "-v", "--tb=short"]
    if specific_test:
        # 例如: TestStockOrder::test_buy_order_basic
        class_name = "Test" + module_name.capitalize() if not specific_test.startswith("Test") else specific_test.split("::")[0]
        if "::" not in specific_test:
            cmd.append(f"{module_file}::{class_name}::{specific_test}")
        else:
            cmd.append(f"{module_file}::{specific_test}")

    print(f"\n{'='*60}")
    print(f"[TEST] 模块: {module_name}")
    print(f"[CMD]  {' '.join(cmd)}")
    print(f"{'='*60}")

    result = subprocess.run(cmd, cwd=TEST_DIR)
    return result.returncode


def send_exit_request(config):
    """测试结束后发送退出请求"""
    if not config.get("exit_on_finish", True):
        print("\n[INFO] exit_on_finish=false，跳过退出请求")
        return

    base_url = config.get("base_url", "https://localhost:19107/v0")
    exit_uri = f"{base_url}/exit"
    try:
        print(f"\n[EXIT] 发送退出请求: {exit_uri}")
        requests.post(exit_uri, verify=False, timeout=5)
        print("[EXIT] 服务已退出")
    except Exception as e:
        print(f"[EXIT] 退出请求失败（服务可能已关闭）: {e}")


def list_modules(config, all_modules):
    """列出所有模块及启用状态"""
    disabled = config.get("disabled_modules", [])
    modules_cfg = config.get("modules", {})

    print(f"\n{'模块':<20} {'状态':<8} {'文件路径'}")
    print("-" * 80)
    for name, path in sorted(all_modules.items()):
        mod_cfg = modules_cfg.get(name, {})
        is_disabled = name in disabled or not mod_cfg.get("enabled", True)
        status = "❌ 禁用" if is_disabled else "✅ 启用"
        print(f"{name:<20} {status:<8} {path}")
    print()


def main():
    config = load_config()
    all_modules = discover_modules()

    # 解析 CLI 参数
    cli_args = [a for a in sys.argv[1:] if not a.startswith("--")]
    flags = [a for a in sys.argv[1:] if a.startswith("--")]

    if "--list" in flags:
        list_modules(config, all_modules)
        return

    # 解析启用的模块
    enabled = resolve_enabled(config, all_modules, cli_args if cli_args else None)

    if not enabled:
        print("[WARN] 没有启用的测试模块")
        send_exit_request(config)
        return

    print(f"\n[INFO] 将运行 {len(enabled)} 个模块: {', '.join(enabled)}")

    # 运行测试
    failed_modules = []
    for module_name in enabled:
        module_file = all_modules.get(module_name)
        if not module_file or not os.path.exists(module_file):
            print(f"[SKIP] 模块文件不存在: {module_name}")
            continue

        # 判断是否有具体的测试用例参数
        specific_test = None
        if cli_args and len(cli_args) > 1:
            specific_test = cli_args[1]

        retcode = run_pytest(module_name, module_file, specific_test)
        if retcode != 0:
            failed_modules.append(module_name)

    # 打印汇总
    print(f"\n{'='*60}")
    print(f"[SUMMARY] 总计: {len(enabled)} 个模块")
    if failed_modules:
        print(f"[FAILED]  失败: {', '.join(failed_modules)}")
    else:
        print(f"[PASSED]  全部通过")
    print(f"{'='*60}")

    # 测试结束后发送退出请求
    send_exit_request(config)

    # 退出码：有失败则返回 1
    sys.exit(1 if failed_modules else 0)


if __name__ == "__main__":
    main()
