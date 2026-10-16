"""
测试前置数据准备：在 QuantService 启动**之前**把测试数据直接灌进各 DuckDB。

为什么必须前置：DuckDB 是单写者模型，服务进程持有库文件后外部进程打不开。
所有步骤都在服务启动前一次性跑完，服务起来后只读不写即可。

数据目录解析与服务端保持一致：ServerConfig::GetDatabasePath() 读 config["server"]["db_path"]，
默认 "data"，且是**相对服务工作目录**解析的。因此本脚本同样按当前工作目录解析，
在 CI 里必须与 QuantService 从同一目录启动。

用法:
  python prepare.py                                  # 跑全部步骤
  python prepare.py --config stock_hist_sim.json     # 从配置读 server.db_path
  python prepare.py margin_stock                     # 只跑某一步
  python prepare.py --db-path data --list

新增预灌项：在 STEPS 里注册一个函数，签名 (ctx) -> None。
"""
import argparse
import json
import struct
import sys
from pathlib import Path

try:
    import duckdb
except ImportError:
    sys.exit("需要 duckdb：pip install duckdb==1.5.4（版本须与服务端 libduckdb 一致）")

# std_header.h — enum contract_type
CONTRACT_TYPES = {
    "stock": 0,
    "future": 1,
    "option": 2,
    "fund": 4,
    "index": 5,
    "exchange_traded_fund": 6,
}

# system.h — enum ExchangeName: MT_Unknow=0, MT_Shenzhen=1, MT_Shanghai=2, MT_Beijing=3
EXCHANGE_BY_PREFIX = {"sh": 2, "sz": 1, "bj": 3}

DEFAULT_DB_PATH = "data"
FINANCE_SUBDIR = "finance"
FINANCE_FILE = "finance.db"


class Context:
    """预灌步骤的共享上下文：数据库根目录 + 待灌标的。"""

    def __init__(self, db_path, symbols):
        self.db_path = Path(db_path)
        self.symbols = symbols

    @property
    def finance_db(self):
        return self.db_path / FINANCE_SUBDIR / FINANCE_FILE

    def connect(self, db_file):
        db_file.parent.mkdir(parents=True, exist_ok=True)
        return duckdb.connect(str(db_file))

    def log(self, message):
        print(f"[prepare] {message}")


def encode_symbol(internal_symbol, contract_type):
    """复刻 QuoteDB::encodeSymbol(to_symbol(...))，返回 int64。

    margin_stock.symbol 存的是 symbol_t（std_header.h，alignas(4) 共 8 字节）的
    原始位模式按 int64 读出：
        bit  0-7    _type      contract_type
        bit  8-15   _exchange  ExchangeName
        bit 16-31   _opt
        bit 32-63   _symbol    uint32，数字代码
    """
    prefix, _, code = internal_symbol.partition(".")
    prefix = prefix.lower()
    if prefix not in EXCHANGE_BY_PREFIX:
        raise ValueError(f"未知市场前缀: {internal_symbol}")
    packed = struct.pack("<BBHI", contract_type, EXCHANGE_BY_PREFIX[prefix], 0, int(code))
    return struct.unpack("<q", packed)[0]


def decode_symbol(encoded):
    """encode_symbol 的逆运算，仅用于打印确认。"""
    ctype, exchange, _, number = struct.unpack("<BBHI", struct.pack("<q", encoded))
    prefix = next((k for k, v in EXCHANGE_BY_PREFIX.items() if v == exchange), "?")
    return f"{prefix}.{number}", ctype


def seed_margin_stock(ctx):
    """把测试标的灌进 FinanceDB.margin_stock，使其被视为可做空。

    SignalNode 在 allowShort=true 时加载 margin_stock 作为可做空标的集合
    (SignalNode::ensureMarginStockData)。表为空时会去调
    `python3 tools/fetch_shortable_stocks.py` 拉真实名单 —— 该脚本不在本仓库，
    拉取失败后 _allowShort 被静默置 false，所有「无持仓的 SELL」降级为 HOLD，
    test_formula_math 的 L4/L6 因此 sell_count=0。

    合成标的也不可能在真实名单里，所以只能预灌。

    _type 取决于 Server::GetContractType() 查 _markets 的结果（查不到兜底 AStock），
    而 _markets 内容依赖运行模式与交易所注册情况。这里对每个标的把全部
    contract_type 都插一遍 —— isMarginStock() 只做 COUNT(*) 匹配，多余的行无副作用。
    """
    db_file = ctx.finance_db
    ctx.log(f"margin_stock → {db_file.resolve()}")

    rows = []
    for symbol in ctx.symbols:
        exchange = symbol.partition(".")[0].upper()
        for type_name, type_value in CONTRACT_TYPES.items():
            rows.append((encode_symbol(symbol, type_value), exchange, type_name))

    conn = ctx.connect(db_file)
    try:
        # FinanceDB::ensureMarginStockTable
        conn.execute("""
            CREATE TABLE IF NOT EXISTS margin_stock (
                symbol      BIGINT NOT NULL,
                exchange    VARCHAR(10) NOT NULL,
                updated_at  TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
                UNIQUE(symbol)
            )
        """)
        conn.execute("CREATE INDEX IF NOT EXISTS idx_margin_stock_exchange "
                     "ON margin_stock(exchange)")
        # 幂等：只清本脚本将写入的行，不动表里其它数据
        for encoded, _, _ in rows:
            conn.execute("DELETE FROM margin_stock WHERE symbol = ?", [encoded])
        conn.executemany(
            "INSERT INTO margin_stock (symbol, exchange, updated_at) "
            "VALUES (?, ?, CURRENT_TIMESTAMP)",
            [(encoded, exchange) for encoded, exchange, _ in rows],
        )
        total = conn.execute("SELECT COUNT(*) FROM margin_stock").fetchone()[0]
    finally:
        conn.close()

    ctx.log(f"margin_stock 写入 {len(rows)} 行（表内共 {total} 行）")
    for encoded, exchange, type_name in rows:
        decoded, ctype = decode_symbol(encoded)
        ctx.log(f"  {decoded:<12} type={type_name:<21}({ctype}) "
                f"exchange={exchange:<3} symbol_id={encoded}")


# 新增预灌项在此注册
STEPS = {
    "margin_stock": seed_margin_stock,
}


def resolve_db_path(db_path, config_path):
    if db_path:
        return db_path
    if config_path:
        config_file = Path(config_path)
        if config_file.exists():
            config = json.loads(config_file.read_text(encoding="utf-8"))
            return config.get("server", {}).get("db_path") or DEFAULT_DB_PATH
        print(f"[prepare] 配置不存在，回退 db_path={DEFAULT_DB_PATH}: {config_file}")
    return DEFAULT_DB_PATH


def main():
    parser = argparse.ArgumentParser(description="测试前置数据准备（服务启动前执行）")
    parser.add_argument("steps", nargs="*", help=f"要执行的步骤，默认全部：{', '.join(STEPS)}")
    parser.add_argument("--config", help="服务配置文件，读 server.db_path")
    parser.add_argument("--db-path", help="直接指定 db_path，覆盖 --config")
    parser.add_argument("--symbols", default="sz.900013,sz.900014",
                        help="内部格式标的，逗号分隔（默认 sz.900013,sz.900014）")
    parser.add_argument("--list", action="store_true", help="列出可用步骤后退出")
    args = parser.parse_args()

    if args.list:
        for name in STEPS:
            print(name)
        return

    unknown = [s for s in args.steps if s not in STEPS]
    if unknown:
        sys.exit(f"未知步骤: {', '.join(unknown)}（可用: {', '.join(STEPS)}）")

    symbols = [s.strip() for s in args.symbols.split(",") if s.strip()]
    if not symbols:
        sys.exit("--symbols 为空")

    db_path = resolve_db_path(args.db_path, args.config)
    print(f"[prepare] duckdb {duckdb.__version__} | db_path={db_path} "
          f"(绝对路径 {(Path(db_path) / FINANCE_SUBDIR / FINANCE_FILE).resolve()}) "
          f"| 工作目录 {Path.cwd()}")

    ctx = Context(db_path, symbols)
    for name in (args.steps or list(STEPS)):
        STEPS[name](ctx)
    print("[prepare] 完成")


if __name__ == "__main__":
    main()
