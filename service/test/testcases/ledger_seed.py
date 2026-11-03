"""账本种子数据（prepare.py 与 test_ledger.py 共用）

为什么单独抽一份：
    账本文件里写什么（prepare.py 的 projection_ledger 步骤）和用例断言什么
    （test_ledger.py 的投影回归）原本是两份手抄数据。合并测试文件时权益期望值
    就漂过一次（667264 → 999214）。这里让两边引用同一份定义，文件内容与断言
    期望值不可能不一致。

这份数据模拟 2026-11-03 的账本投影失明事故现场：
    初始 1,000,000 → 建仓 4 只股票共 1,331,950 → 交易费用约 786
    → 现金 −332,736（超配），但账本里持仓齐全
"""
import json
from pathlib import Path

# 账本初始资金
INITIAL_CAPITAL = 1_000_000.0

# 投影回归用例使用的策略名
PROJECTION_STRATEGY = "test_ledger_projection_regression"

# 事故现场的 4 个持仓：symbol → (数量, 成本价)
PROJECTION_POSITIONS = {
    "sz.300450": (11400, 29.15),
    "sz.002241": (14900, 22.64),
    "sh.601939": (27600, 12.04),
    "sh.688981": (3300, 100.00),
}

# 建仓交易费用（事故当天四笔建仓的费用合计）
PROJECTION_FEE = 786.0

# 持仓市值：种子把 current_price 写成成本价，所以市值 == 建仓成本合计
PROJECTION_POSITION_VALUE = sum(qty * cost for qty, cost in PROJECTION_POSITIONS.values())  # 1,331,950

# 现金 = 初始资金 − 建仓成本 − 交易费用 = −332,736（超配现场）
PROJECTION_CASH = INITIAL_CAPITAL - PROJECTION_POSITION_VALUE - PROJECTION_FEE

# 权益 = 现金 + 持仓市值 − 融资余额（融资余额为 0）
PROJECTION_EQUITY = PROJECTION_CASH + PROJECTION_POSITION_VALUE


def build_projection_account() -> dict:
    """构造投影回归账户（账本文件里 accounts[strategy] 的内容）"""
    return {
        "allocated": INITIAL_CAPITAL,
        "cash": PROJECTION_CASH,
        "profit": 0.0,
        "active": True,
        "positions": {
            sym: {
                "quantity": qty,
                "avg_cost": cost,
                # 简化：当前价 = 成本价，持仓市值即建仓成本合计
                "current_price": cost,
                "is_margin": False,
                "margin_ratio": 1.0,
            }
            for sym, (qty, cost) in PROJECTION_POSITIONS.items()
        },
        "lots": {},
        "credit": {
            "borrowedCash": 0.0,
            "borrowedShares": {},
            "shortAvgPrice": {},
            "financedAmount": {},
        },
    }


def read_ledger_file(path) -> dict:
    """读取账本文件；不存在时返回空账本骨架"""
    path = Path(path)
    if path.exists():
        data = json.loads(path.read_text(encoding="utf-8"))
        if not isinstance(data, dict):
            data = {}
    else:
        data = {}
    data.setdefault("version", 1)
    data.setdefault("initialCapital", INITIAL_CAPITAL)
    data.setdefault("accounts", {})
    return data


def write_ledger_file(path, data: dict) -> None:
    """写回账本文件"""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2), encoding="utf-8")


def set_account(data: dict, strategy: str, account: dict) -> dict:
    """把账户写进账本数据（覆盖同名账户）"""
    data.setdefault("accounts", {})[strategy] = account
    return data


def remove_account(data: dict, strategy: str) -> dict:
    """从账本数据里移除账户（不存在时不动）"""
    data.get("accounts", {}).pop(strategy, None)
    return data
