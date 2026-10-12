#!/usr/bin/env python3
"""把策略 JSON 里 function 节点的 range 从时间单位迁移为 bar 单位。

背景：range 原本是 "20d" 这类时间字符串，需要按上游数据频率换算成 bar 数
（TimeStringToMinutes / MinutesPerBar）。多频率策略下这会让「同一份配置挂在
不同频率的输入上」产生歧义，因此改为直接以 bar 计数。

换算基准与迁移前的 C++ 完全一致（1 交易日 = 240 分钟）：
  TimeStringToMinutes:  s→1  m→value  h→value*60  d→value*240  无单位→分钟数
  MinutesPerBar:        Day/其他→240, Min1→1, Min5→5, Second→1

为什么做文本级改写而不是 JSON 重序列化：
  1. 存量文件格式不统一（golden_cross 用紧凑内联对象，CTA_v16 用 \\u 转义 +
     排序键），整体重序列化会产生上千行与本改动无关的 diff，无法评审。
  2. 前端会把整份 sourceNode 复制进 edges[]，同一份 range 在一个文件里出现多次。
     文本级改写能保证 nodes[] 与 edges[] 两处同步更新，而 JSON 往返只会改前者，
     留下不一致的副本。

安全性：只改写 "range": { ... } 且其 "value" 匹配 ^\\d+[smhd]$ 的对象，
其他同名键（date_range / win_rate_range 等）不受影响。

用法：
    python3 migrate_range_to_bars.py --dry-run
    python3 migrate_range_to_bars.py --apply
"""

import argparse
import json
import os
import re
import sys

MINUTES_PER_BAR = {
    "1d": 240, "daily": 240, "1m": 1, "5m": 5, "second": 1, "1s": 1,
}
MINUTES_PER_UNIT = {"s": 1, "m": 1, "h": 60, "d": 240}

NEW_PLACEHOLDER = "正整数，窗口长度（bar 数）"
NEW_ERROR = "格式错误，必须为正整数（窗口长度以 bar 计）"
NEW_PATTERN = r"^\d+$"

# "range": { ... } —— range 参数对象内部不含嵌套花括号
RANGE_OBJ_RE = re.compile(r'"range"\s*:\s*\{([^{}]*)\}')
VALUE_RE = re.compile(r'("value"\s*:\s*)"(\d+)([smhdSMHD])"')
LEGACY_RE = re.compile(r'^(\d+)([smhdSMHD])?$')


def legacy_to_bars(raw: str, freq: str):
    """按迁移前的 C++ 逻辑换算；不是合法带单位写法时返回 None。"""
    m = LEGACY_RE.match(raw.strip())
    if not m:
        return None
    value = int(m.group(1))
    if value <= 0:
        return None
    unit = m.group(2)
    minutes = value if unit is None else value * MINUTES_PER_UNIT[unit.lower()]
    mpb = MINUTES_PER_BAR.get(freq, 240)
    return max(1, minutes // mpb)


def sole_input_freq(strategy: dict):
    """返回 (freq, 是否唯一)。频率不唯一时放弃该文件，交人工处理。"""
    freqs = set()
    for node in strategy.get("nodes", []):
        if not isinstance(node, dict):
            continue
        data = node.get("data")
        if not isinstance(data, dict) or data.get("nodeType") != "input":
            continue
        params = data.get("params") or {}
        freq = params.get("freq") or {}
        val = freq.get("value") if isinstance(freq, dict) else freq
        freqs.add(val or "1d")
    if not freqs:
        return None, False
    if len(freqs) > 1:
        return None, False
    return next(iter(freqs)), True


def rewrite_obj_body(body: str, freq: str, path: str, report: list):
    """改写单个 range 参数对象的内容，返回 (新内容, 是否变化)。"""
    vm = VALUE_RE.search(body)
    if not vm:
        return body, False
    # group(2)=数字部分，group(3)=单位后缀；两者都要传给换算函数
    raw_value = vm.group(2) + (vm.group(3) or "")
    bars = legacy_to_bars(raw_value, freq)
    if bars is None:
        report.append((path, "SKIP-unparsable", raw_value))
        return body, False

    new_body = VALUE_RE.sub(lambda m: f'{m.group(1)}{bars}', body, count=1)

    def esc(val):
        return val.replace("\\", "\\\\").replace('"', '\\"')

    if "pattern" in new_body:
        new_body = re.sub(r'("pattern"\s*:\s*)"(?:[^"\\]|\\.)*"',
                          lambda m: f'{m.group(1)}"{esc(NEW_PATTERN)}"', new_body, count=1)
    if "placeholder" in new_body:
        new_body = re.sub(r'("placeholder"\s*:\s*)"(?:[^"\\]|\\.)*"',
                          lambda m: f'{m.group(1)}"{esc(NEW_PLACEHOLDER)}"', new_body, count=1)
    if "errorMsg" in new_body:
        new_body = re.sub(r'("errorMsg"\s*:\s*)"(?:[^"\\]|\\.)*"',
                          lambda m: f'{m.group(1)}"{esc(NEW_ERROR)}"', new_body, count=1)
    if '"type"' in new_body:
        new_body = re.sub(r'("type"\s*:\s*)"text"', r'\1"number"', new_body, count=1)

    report.append((path, f"{vm.group(0)} -> {bars} bars (freq={freq})", ""))
    return new_body, True


def iter_json_files(roots):
    skip_dirs = {"node_modules", ".git", "dist", "build", ".next", "__pycache__"}
    for root in roots:
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if d not in skip_dirs]
            for fn in filenames:
                if fn.endswith(".json"):
                    yield os.path.join(dirpath, fn)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--apply", action="store_true")
    ap.add_argument("--root", action="append", default=None)
    args = ap.parse_args()
    if not (args.dry_run or args.apply):
        ap.error("choose --dry-run or --apply")

    roots = args.root or ["."]
    report, touched, skipped = [], 0, []

    for path in iter_json_files(roots):
        try:
            raw = open(path, encoding="utf-8").read()
            strategy = json.loads(raw)
        except Exception:
            continue
        if not isinstance(strategy, dict) or "nodes" not in strategy:
            continue
        if not any(isinstance(n, dict) and isinstance(n.get("data"), dict)
                   and n["data"].get("nodeType") == "function"
                   for n in strategy.get("nodes", [])):
            continue

        freq, unique = sole_input_freq(strategy)
        if not unique:
            skipped.append(path)
            continue
        freq = freq or "1d"

        changed = [False]

        def sub(m):
            body, ch = rewrite_obj_body(m.group(1), freq, path, report)
            changed[0] = changed[0] or ch
            return '"range": {' + body + '}'

        new_raw = RANGE_OBJ_RE.sub(sub, raw)
        if changed[0]:
            touched += 1
            if args.apply:
                open(path, "w", encoding="utf-8").write(new_raw)

    for row in report:
        print("  ", *row)
    for p in skipped:
        print(f"   SKIP-ambiguous-freq {p}")
    mode = "APPLIED" if args.apply else "DRY-RUN"
    print(f"\n{mode}: {touched} files, {len(report)} range params, {len(skipped)} skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main())