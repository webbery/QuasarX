#!/usr/bin/env python3
"""
两融（融资融券）明细数据抓取脚本

数据来源（akshare，文档见 https://akshare.akfamily.xyz/data/stock/stock_flow.html#id63）:
  - 上交所: stock_margin_detail_sse  融资融券明细（单位：元 / 股）
  - 深交所: stock_margin_detail_szse  融资融券明细（单位：元 / 股）

（北交所 stock_margin_detail_bse 暂不接入：bse.cn 常年连不上，两融标的也极少，
  为了它每天在日志里留一条失败告警不划算。需要时按下面同样的模式加回来即可。）

输出:
  1) CSV 文件（写入 --out-dir 指定的目录），列名沿用服务端 FinanceDB 的通用类目约定
     （code / pubDate / statDate + 类目字段），可直接被 FinanceDB::importCsv 导入:
       detail_<YYYYMMDD>.csv
     给了 --symbols 时只保留名单内的标的（交易所接口按日返回全市场，
     没有逐标的查询的口子，只能抓回来再筛）
         code          内部符号格式（sh.600000 / sz.000001 / bj.920000）
         pubDate       可用日 = 交易日 + 1 天（交易所盘后才公布，避免回测前视）
         statDate      交易日
         rzBalance     融资余额（元）
         rzBuy         融资买入额（元）
         rzRepay       融资偿还额（元，仅上交所有）
         rqVolume      融券余量（股）
         rqSell        融券卖出量（股）
         rqRepay       融券偿还量（股，仅上交所有）
         rqBalance     融券余额（元）
         totalBalance  融资融券余额（元）
     交易所的明细接口按日返回全市场，没有逐标的查询的口子，
     所以「下载啥就存啥」——沪深两市的全部两融标的都会入库
  2) stdout 打印一行 JSON manifest，服务端据此判断成败、拿到 CSV 路径:
       {"ok": true, "date": "2026-11-10", "rows": 3750, "file": "...", "sources": [...]}

退出码:
  0 = 成功
  2 = 参数错误
  3 = 全部数据源抓取失败（或目标日期无任何数据）
  4 = 部分数据源抓取失败（其余成功，CSV 里只有成功部分）
  5 = 运行环境缺失（akshare / pandas 未安装）

用法:
  # 服务端日常调用（自动取最近一个已有数据的交易日）
  python fetch_margin_data.py --out-dir ../build/data/margin

  # 指定交易日下载（补数用）
  python fetch_margin_data.py --date 20261110 --out-dir ../build/data/margin

  # 人类可读摘要（本地排查用）
  python fetch_margin_data.py --out-dir ../build/data/margin --summary
"""
import argparse
import json
import os
import sys
from datetime import datetime, timedelta

EXIT_OK = 0
EXIT_ARG = 2
EXIT_ALL_FAILED = 3
EXIT_PARTIAL = 4
EXIT_NO_ENV = 5

# 交易所标识与代码前缀（代码要带市场前缀：服务端 toInternalSymbol 的兜底规则
# 只认 6/5 开头判沪市、其余判深市，裸代码会被错标交易所）
EXCHANGE_SSE = "SSE"
EXCHANGE_SZSE = "SZSE"

PREFIX = {EXCHANGE_SSE: "sh.", EXCHANGE_SZSE: "sz."}

# CSV 列名（与服务端 FinanceDB 的 "margin_daily" 类目字段映射一一对应）
CSV_HEADER = [
    "code", "pubDate", "statDate",
    "rzBalance", "rzBuy", "rzRepay",
    "rqVolume", "rqSell", "rqRepay",
    "rqBalance", "totalBalance",
]


def log(msg):
    """诊断信息一律走 stderr —— stdout 只允许出现 JSON manifest。"""
    print(msg, file=sys.stderr)


# ============================================================
#  通用工具
# ============================================================

def to_float(value):
    """宽松转 float：空值/NaN/异常统一返回 None（CSV 里留空，不写 0）。"""
    if value is None:
        return None
    try:
        if isinstance(value, str):
            value = value.strip().replace(",", "")
            if value in ("", "-", "--", "None", "nan"):
                return None
        f = float(value)
        if f != f:  # NaN
            return None
        return f
    except (TypeError, ValueError):
        return None


def pick_col(df, candidates, contains=False):
    """按候选列名挑列：先精确匹配，再按包含匹配（akshare 改列名时不至于整体崩）。"""
    for name in candidates:
        if name in df.columns:
            return name
    if contains:
        for col in df.columns:
            for name in candidates:
                if name in str(col):
                    return col
    return None


def get_val(row, df, candidates, contains=True):
    """从一行里取数值列（找不到列或值不可解析时返回 None）。"""
    col = pick_col(df, candidates, contains)
    if col is None:
        return None
    return to_float(row.get(col))


def norm_code(value):
    """证券代码归一：去掉非数字字符，补齐 6 位。"""
    if value is None:
        return ""
    digits = "".join(ch for ch in str(value) if ch.isdigit())
    if not digits:
        return ""
    return digits.zfill(6)


def norm_date(value):
    """日期归一为 YYYYMMDD（akshare 各接口的日期格式不统一）。"""
    if value is None:
        return ""
    digits = "".join(ch for ch in str(value) if ch.isdigit())
    if len(digits) >= 8:
        return digits[:8]
    return ""


def dash_date(compact):
    """YYYYMMDD -> YYYY-MM-DD。"""
    if len(compact) == 8 and compact.isdigit():
        return f"{compact[:4]}-{compact[4:6]}-{compact[6:]}"
    return compact


def next_day_dash(compact):
    """交易日 + 1 天（YYYY-MM-DD）：交易所盘后公布两融数据，
    当日 15:00 的决策看不到当日数据，用「次日可用」避免回测前视。"""
    try:
        d = datetime.strptime(dash_date(compact), "%Y-%m-%d") + timedelta(days=1)
        return d.strftime("%Y-%m-%d")
    except ValueError:
        return dash_date(compact)


def parse_date_arg(value):
    """接受 YYYYMMDD / YYYY-MM-DD，统一成 YYYYMMDD。"""
    if not value:
        return ""
    digits = "".join(ch for ch in value if ch.isdigit())
    if len(digits) != 8:
        raise ValueError(f"日期格式非法: {value}（应为 YYYYMMDD 或 YYYY-MM-DD）")
    return digits


# ============================================================
#  数据抓取（三所个股明细，单位：元 / 股）
# ============================================================

def fetch_sse_detail(date_compact, ak):
    """上交所个股明细。"""
    try:
        df = ak.stock_margin_detail_sse(date=date_compact)
        if df is None or df.empty:
            return [], None
        rows = []
        for _, row in df.iterrows():
            code = norm_code(row.get("标的证券代码"))
            if not code:
                continue
            stat = norm_date(row.get("信用交易日期")) or date_compact
            rows.append({
                "code": PREFIX[EXCHANGE_SSE] + code,
                "statDate": dash_date(stat),
                "rzBalance": get_val(row, df, ["融资余额"]),
                "rzBuy": get_val(row, df, ["融资买入额"]),
                "rzRepay": get_val(row, df, ["融资偿还额"]),
                "rqVolume": get_val(row, df, ["融券余量"]),
                "rqSell": get_val(row, df, ["融券卖出量"]),
                "rqRepay": get_val(row, df, ["融券偿还量"]),
                "rqBalance": None,          # 沪市明细不给融券余额（只有汇总给）
                "totalBalance": None,
            })
        return rows, None
    except Exception as e:
        return [], f"{type(e).__name__}: {e}"


def fetch_szse_detail(date_compact, ak):
    """深交所个股明细。"""
    try:
        df = ak.stock_margin_detail_szse(date=date_compact)
        if df is None or df.empty:
            return [], None
        rows = []
        for _, row in df.iterrows():
            code = norm_code(row.get("证券代码"))
            if not code:
                continue
            rows.append({
                "code": PREFIX[EXCHANGE_SZSE] + code,
                "statDate": dash_date(date_compact),
                "rzBalance": get_val(row, df, ["融资余额"]),
                "rzBuy": get_val(row, df, ["融资买入额"]),
                "rzRepay": None,
                "rqVolume": get_val(row, df, ["融券余量"]),
                "rqSell": get_val(row, df, ["融券卖出量"]),
                "rqRepay": None,
                "rqBalance": get_val(row, df, ["融券余额"]),
                "totalBalance": get_val(row, df, ["融资融券余额"]),
            })
        return rows, None
    except Exception as e:
        return [], f"{type(e).__name__}: {e}"


def resolve_latest_trade_date(ak):
    """探测最近一个已有两融数据的交易日（YYYYMMDD）。
    目前只能用上交所汇总（区间接口，返回最后一行即最新日）。"""
    try:
        df = ak.stock_margin_sse(start_date="20100101", end_date="20991231")
        if df is not None and not df.empty:
            col = pick_col(df, ["信用交易日期"])
            if col:
                last = norm_date(df[col].iloc[-1])
                if last:
                    return last
    except Exception as e:
        log(f"[warn] 上交所汇总探测交易日失败: {e}")
    return ""


def collect_detail(date_compact, ak):
    """沪深两市个股明细合并。"""
    sources = []
    rows = []
    for name, fn in (
        (EXCHANGE_SSE, fetch_sse_detail),
        (EXCHANGE_SZSE, fetch_szse_detail),
    ):
        got, err = fn(date_compact, ak)
        rows.extend(got)
        sources.append({"name": name, "ok": err is None, "rows": len(got), "error": err or ""})
    return rows, sources


def write_csv(path, rows):
    """写 CSV。列名用通用类目约定；空值留空（服务端写库为 NULL）。"""
    import pandas as pd

    df = pd.DataFrame(rows)
    for col in CSV_HEADER:
        if col not in df.columns:
            df[col] = None
    df = df[CSV_HEADER]
    df.to_csv(path, index=False, encoding="utf-8-sig")
    return path


def print_summary(rows, sources, date_compact):
    """人类可读摘要（本地排查用）。"""
    print("=" * 70)
    print(f"两融明细  交易日: {dash_date(date_compact)}")
    print("=" * 70)
    for src in sources:
        status = "OK  " if src["ok"] else "FAIL"
        print(f"  [{status}] {src['name']:<8} rows={src['rows']}"
              + (f"  error={src['error']}" if src["error"] else ""))
    print("-" * 70)
    print(f"合计 {len(rows)} 行")
    for row in rows[:5]:
        print("  " + "  ".join(f"{k}={v}" for k, v in row.items() if v is not None))
    if len(rows) > 5:
        print(f"  ... 共 {len(rows)} 行")
    print("=" * 70)


def main():
    parser = argparse.ArgumentParser(
        description="两融（融资融券）明细数据抓取脚本",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  python fetch_margin_data.py --out-dir ../build/data/margin
  python fetch_margin_data.py --date 20261110 --out-dir ../build/data/margin
  python fetch_margin_data.py --date 20261110 --out-dir ... --summary
        """,
    )
    parser.add_argument("--date", type=str, default="",
                        help="交易日 YYYYMMDD 或 YYYY-MM-DD；不填则自动取最近有数据的交易日")
    parser.add_argument("--out-dir", type=str, default="",
                        help="CSV 输出目录（默认当前目录下的 data/margin）")
    parser.add_argument("--summary", action="store_true", help="打印人类可读摘要")
    args = parser.parse_args()

    try:
        import akshare as ak
        import pandas as pd  # noqa: F401  (写 CSV 用)
    except ImportError as e:
        log(f"错误: 缺少依赖 ({e})，请在服务实际使用的 python 环境里安装: pip install akshare pandas")
        print(json.dumps({"ok": False, "error": f"missing dependency: {e}"}, ensure_ascii=False))
        return EXIT_NO_ENV

    # ── 日期 ──
    requested = ""
    if args.date:
        try:
            requested = parse_date_arg(args.date)
        except ValueError as e:
            log(f"错误: {e}")
            print(json.dumps({"ok": False, "error": str(e)}, ensure_ascii=False))
            return EXIT_ARG

    date_compact = requested
    if not date_compact:
        date_compact = resolve_latest_trade_date(ak)
        if not date_compact:
            log("错误: 无法确定最近交易日（上交所汇总接口不可用）")
            print(json.dumps({"ok": False, "error": "cannot resolve latest trade date"},
                             ensure_ascii=False))
            return EXIT_ALL_FAILED

    # ── 抓取 ──
    rows, sources = collect_detail(date_compact, ak)
    failed = [s for s in sources if not s["ok"]]
    empty = [s for s in sources if s["ok"] and s["rows"] == 0]

    # 目标日期无数据：显式指定日期时视为失败（静默换成别的日期会让人误以为补到了那天）；
    # 自动解析日期时回退到最近一个交易日（节假日属正常情况）。
    if not rows:
        if requested:
            log(f"错误: {dash_date(date_compact)} 无两融数据（各数据源均返回空或失败）")
            print(json.dumps({
                "ok": False, "date": dash_date(date_compact),
                "requested_date": dash_date(requested), "rows": 0,
                "sources": sources,
                "error": "no data for requested trade date",
            }, ensure_ascii=False))
            return EXIT_ALL_FAILED

        fallback = resolve_latest_trade_date(ak)
        if not fallback or fallback == date_compact:
            log("错误: 所有数据源均无数据")
            print(json.dumps({
                "ok": False, "date": dash_date(date_compact),
                "rows": 0, "sources": sources, "error": "no data available",
            }, ensure_ascii=False))
            return EXIT_ALL_FAILED

        log(f"[info] {dash_date(date_compact)} 无数据，回退到最近交易日 {dash_date(fallback)}")
        date_compact = fallback
        rows, sources = collect_detail(date_compact, ak)
        failed = [s for s in sources if not s["ok"]]

    # ── 落盘 ──
    out_dir = args.out_dir or os.path.join("data", "margin")
    os.makedirs(out_dir, exist_ok=True)
    pub_date = next_day_dash(date_compact)
    for r in rows:
        r["pubDate"] = pub_date
    csv_path = os.path.join(out_dir, f"detail_{date_compact}.csv")
    write_csv(csv_path, rows)

    fetch_ok = len(failed) == 0 and len(rows) > 0
    manifest = {
        # fetch_ok: 所有数据源都拿到了数据（某个源失败 → false，CSV 里只有成功部分）
        "fetch_ok": fetch_ok,
        "date": dash_date(date_compact),
        "pub_date": pub_date,
        "requested_date": dash_date(requested) if requested else "",
        "updated_at": datetime.now().strftime("%Y-%m-%dT%H:%M:%S"),
        "out_dir": os.path.abspath(out_dir),
        "file": os.path.abspath(csv_path),
        "rows": len(rows),
        "sources": sources,
    }
    if failed:
        manifest["error"] = "; ".join(f"{s['name']}: {s['error']}" for s in failed)
    if empty:
        manifest["empty_sources"] = [s["name"] for s in empty]

    print(json.dumps(manifest, ensure_ascii=False))

    if args.summary:
        print_summary(rows, sources, date_compact)

    if not rows:
        return EXIT_ALL_FAILED
    if failed:
        return EXIT_PARTIAL
    return EXIT_OK


if __name__ == "__main__":
    sys.exit(main())
