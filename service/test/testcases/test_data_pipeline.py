#!/usr/bin/env python3
"""
数据下载管线测试（Data / DownloadBase / DataPipeline）

覆盖范围：
  1. 下载脚本离线契约 —— 财务窗口季度解析、两融 pubDate=statDate+1、
     行情 CSV 表头与 QuoteData::ExpectedColumns 逐列一致
  2. HTTP 提交入口校验 —— /quote、/finance、/margin、/dividend、/finance/data
     缺参/非法参数必须在派发后台任务之前返回 400
  3. 本地导入链路（无网络，全部用真实标的）——
     - FinanceDB::importCsv 的列名映射：CSV 用 baostock 真实表头
       （AssetTurnRatio / liabilityToAsset / CFOToOR / dupont*），入库后
       对应 DB 列必须非空（列名写错会表现为整列 NULL）
     - 单次导入 > 500 行：验证分块写库没有整批失败
     - 坏 statDate 行只跳过该行，不拖垮整批
     - margin_daily 表结构 + /margin 概览
     - 分红导入的 symbols 作用域：目录里 scope 外的 CSV 不得入库
  4. 真实 pipeline（网络，默认 skip，RUN_DATA_PIPELINE_TESTS=1 启用）——
     通过 HTTP 触发 /quote、/finance、/margin，全部用真实标的 + 有限窗口：
       - 行情：标的必须已在库里，增量只补最新几天，不重下全历史
       - 基本面：只取 2024-Q1~Q4 四个季度（Check 的最低季度数）
       - 两融：只抓一个已有交易日

为什么导入链路走 /finance/data 而不是直接跑 pipeline：
  pipeline 的 Download 阶段必然调外部数据源（BaoStock/akshare），CI 无外网。
  列名映射错、单条 INSERT 超过 500 行这两类最容易回退的问题，走本地导入
  入口即可完整覆盖，不需要 mock C++ 内部。

补跑状态（RefreshState）没有 HTTP 触发入口，其“目标交易日未刷新则补跑”
属于调度行为，按重构方案的验证清单手工验证（改 data_refresh_state.json
后重启服务），本文件不重复覆盖。
"""
import importlib
import importlib.util
import json
import os
import sys
import time
import types
from datetime import date, datetime, timedelta

import pytest
import requests

from tool import BASE_URL, SERVICE_ROOT, check_response

TOOLS_DIR = SERVICE_ROOT / "tools"

# 真实下载依赖外网；默认关闭，避免 CI 被外部数据源状态拖垮
RUN_NETWORK = os.environ.get("RUN_DATA_PIPELINE_TESTS", "").lower() in ("1", "true", "yes")
needs_network = pytest.mark.skipif(
    not RUN_NETWORK,
    reason="需要 RUN_DATA_PIPELINE_TESTS=1（依赖 BaoStock/akshare 与外网）",
)

# 本地导入用的真实标的候选。挑选规则：该标的在该类目表里没有任何行，
# 这样导入/删除不会碰到真实数据。
REAL_STOCK_CANDIDATES = [
    "sh.600000", "sh.600009", "sh.600018", "sh.600028", "sh.600030",
    "sh.600036", "sz.000001", "sz.000002", "sz.000063", "sz.000100",
]
QUOTE_HEADER = ["datetime", "open", "close", "high", "low", "volume", "turnover"]


# ═══════════════════════════════════════════════════════════
#  公共工具
# ═══════════════════════════════════════════════════════════

def _kwargs(auth_token, **extra):
    kwargs = {"verify": False}
    if auth_token and len(auth_token) > 10:
        kwargs["headers"] = {"Authorization": auth_token}
    kwargs.update(extra)
    return kwargs


def _load_tool(filename, stub_modules=()):
    """按文件路径加载 tools/ 下的脚本，不执行 main()

    fetch_finance_data.py / download_quote_bs.py 顶层 import baostock、pandas，
    缺依赖会直接 sys.exit(1)。测试只调用纯函数，缺的依赖先塞占位模块；
    加载完立刻把占位模块撤掉，避免影响后续 pytest.importorskip("pandas")。
    """
    injected = []
    for name in stub_modules:
        try:
            importlib.import_module(name)
        except ImportError:
            sys.modules[name] = types.ModuleType(name)
            injected.append(name)

    try:
        path = TOOLS_DIR / filename
        spec = importlib.util.spec_from_file_location(f"data_pipeline_{path.stem}", path)
        assert spec is not None and spec.loader is not None
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        for name in injected:
            sys.modules.pop(name, None)
    return module


def _next_day(ymd):
    return (datetime.strptime(ymd, "%Y-%m-%d") + timedelta(days=1)).strftime("%Y-%m-%d")


def _wait_until(fn, timeout, interval=2.0, message="条件"):
    """轮询等待某个谓词成立（pipeline 是后台任务，SSE 可能被调度器事件干扰）"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if fn():
            return
        time.sleep(interval)
    raise AssertionError(f"等待{message}超时（{timeout}s）")


# ── SSE：等待后台 pipeline 发来的完成事件 ──

def _open_sse(auth_token, timeout=300):
    headers = {"Accept": "text/event-stream"}
    if auth_token and len(auth_token) > 10:
        headers["Authorization"] = auth_token
    resp = requests.get(f"{BASE_URL}/server/event", stream=True,
                        headers=headers, verify=False, timeout=timeout)
    assert resp.status_code == 200, "SSE 连接失败"
    # 订阅发生在 content provider 第一次回调时，给服务端一点时间挂上分发器
    time.sleep(0.5)
    return resp


def _wait_sse(resp, event_name, timeout, predicate=lambda d: True):
    """从 SSE 流里等一个指定事件

    服务端格式（format_sse）：
        event:<name>\\n id:<n>\\n data:<json>\\n\\n
    这里按 event / data 成对解析，跳过 keepalive 注释行与其它事件。
    """
    deadline = time.time() + timeout
    current = ""
    for line in resp.iter_lines(decode_unicode=True):
        if time.time() > deadline:
            break
        if not line:
            continue
        if line.startswith("event:"):
            current = line[len("event:"):].strip()
            continue
        if not line.startswith("data:"):
            continue
        if current != event_name:
            continue
        try:
            payload = json.loads(line[len("data:"):].strip())
        except json.JSONDecodeError:
            continue
        if predicate(payload):
            return payload
    raise AssertionError(f"等待 SSE 事件 {event_name} 超时（{timeout}s）")


# ── FinanceDB HTTP 辅助 ──

def _finance_import(auth_token, category, code, lines):
    resp = requests.post(
        f"{BASE_URL}/finance/data",
        json={"action": "import", "category": category, "code": code, "data": lines},
        **_kwargs(auth_token),
    )
    return check_response(resp)


def _finance_query(auth_token, category, code, limit=500, start="", end=""):
    params = {"category": category, "code": code, "limit": limit}
    if start:
        params["start"] = start
    if end:
        params["end"] = end
    resp = requests.get(f"{BASE_URL}/finance", params=params, **_kwargs(auth_token))
    return check_response(resp)


def _finance_delete(auth_token, category, code):
    requests.delete(
        f"{BASE_URL}/finance",
        params={"category": category, "code": code},
        **_kwargs(auth_token),
    )


def _count_finance_rows(auth_token, category, code):
    """某标的在某类目表里的行数；表还不存在时按 0 处理"""
    data = _finance_query(auth_token, category, code, limit=1)
    if not isinstance(data, dict):
        return 0
    return int(data.get("count") or 0)


def _pick_empty_symbol(auth_token, category):
    """挑一个该标的在该类目表里没有数据的真实标的

    这样本地导入/删除不会覆盖或删掉真实财务数据。
    """
    for sym in REAL_STOCK_CANDIDATES:
        if _count_finance_rows(auth_token, category, sym) == 0:
            return sym
    pytest.skip(f"{category} 表里候选真实标的都已有数据，跳过以免碰真实数据")


# ═══════════════════════════════════════════════════════════
#  1. 下载脚本离线契约（不需要服务、不需要网络）
# ═══════════════════════════════════════════════════════════

class TestDownloadScriptContracts:
    """tools/ 下脚本的离线契约 —— 这些格式一旦漂移，C++ 侧会静默错位"""

    def test_finance_window_accepts_year_quarter_and_date(self):
        """--start/--end 支持 2024 / 2024-Q3 / 2024-07-01"""
        mod = _load_tool("fetch_finance_data.py", ("baostock", "pandas"))

        assert mod.parse_year_quarter("2024-Q3", is_end=False) == (2024, 3)
        assert mod.parse_year_quarter("2024Q4", is_end=False) == (2024, 4)
        assert mod.parse_year_quarter("2024-07-01", is_end=False) == (2024, 3)
        # 只给年份时：起点取 Q1，终点取 Q4
        assert mod.parse_year_quarter("2024", is_end=False) == (2024, 1)
        assert mod.parse_year_quarter("2024", is_end=True) == (2024, 4)

        with pytest.raises(ValueError):
            mod.parse_year_quarter("2024-Q5", is_end=False)
        with pytest.raises(ValueError):
            mod.parse_year_quarter("not-a-date", is_end=False)

    def test_finance_window_maps_to_stat_date_range(self):
        """(year, quarter) → statDate 过滤区间

        这是「--start 2024-Q3 却拉了 2024 全年」那个缺陷的回归点：
        Baostock 只按季度取数，必须再按 statDate 收窗口。
        """
        mod = _load_tool("fetch_finance_data.py", ("baostock", "pandas"))

        assert mod.quarter_date_range((2024, 3), None) == ("2024-07-01", "2024-09-30")
        assert mod.quarter_date_range((2024, 4), (2025, 1)) == ("2024-10-01", "2025-03-31")
        assert mod.quarter_date_range((2024, 1), (2024, 2)) == ("2024-01-01", "2024-06-30")
        assert mod.quarter_date_range(None, None) == (None, None)

    def test_margin_pub_date_is_next_day(self):
        """两融 pubDate = statDate + 1 天（避免当日决策用到当日盘后数据）"""
        mod = _load_tool("fetch_margin_data.py")

        assert mod.parse_date_arg("20261110") == "20261110"
        assert mod.parse_date_arg("2026-11-10") == "20261110"
        assert mod.next_day_dash("20261110") == "2026-11-11"
        assert mod.next_day_dash("20260228") == "2026-03-01"   # 跨月
        assert mod.next_day_dash("20261231") == "2027-01-01"   # 跨年

        with pytest.raises(ValueError):
            mod.parse_date_arg("2026-11")

    def test_margin_csv_header_matches_finance_category(self):
        """两融 CSV 列名必须与 FinanceDB 的 margin_daily 字段映射逐字一致"""
        mod = _load_tool("fetch_margin_data.py")

        assert mod.CSV_HEADER == [
            "code", "pubDate", "statDate",
            "rzBalance", "rzBuy", "rzRepay",
            "rqVolume", "rqSell", "rqRepay",
            "rqBalance", "totalBalance",
        ]

    def test_quote_csv_header_is_positional_contract(self, tmp_path):
        """行情 CSV 表头/列序必须等于 QuoteData::ExpectedColumns()

        QuoteDB::importCsv 是按位置解析的 —— 表头顺序一变，价格列就整体错位。
        """
        pd = pytest.importorskip("pandas")
        mod = _load_tool("download_quote_bs.py", ("baostock",))

        df = pd.DataFrame({
            "datetime": pd.to_datetime(["2024-01-02 00:00:00"]),
            "open": [10.0],
            "close": [10.5],
            "high": [10.6],
            "low": [9.9],
            "volume": [100000],
            "turnover": [1050000.0],
        })
        out = tmp_path / "quote.csv"
        assert mod.save_csv(df, str(out)) is True

        header = out.read_text(encoding="utf-8").splitlines()[0].strip().split(",")
        assert header == QUOTE_HEADER


# ═══════════════════════════════════════════════════════════
#  2. 提交入口参数校验（HTTP，不触发外部下载）
# ═══════════════════════════════════════════════════════════

class TestPipelineSubmitValidation:
    """非法请求必须在派发后台任务之前被挡掉（400）"""

    @pytest.mark.timeout(10)
    def test_quote_rejects_invalid_json(self, auth_token):
        resp = requests.post(f"{BASE_URL}/quote", data="{bad json",
                            **_kwargs(auth_token))
        assert resp.status_code == 400

    @pytest.mark.timeout(10)
    def test_quote_requires_symbols(self, auth_token):
        resp = requests.post(f"{BASE_URL}/quote", json={}, **_kwargs(auth_token))
        assert resp.status_code == 400
        assert "symbols" in resp.json()["message"]

    @pytest.mark.timeout(10)
    def test_finance_rejects_invalid_json(self, auth_token):
        resp = requests.post(f"{BASE_URL}/finance", data="{bad json",
                            **_kwargs(auth_token))
        assert resp.status_code == 400

    @pytest.mark.timeout(10)
    def test_finance_requires_symbols_or_code(self, auth_token):
        resp = requests.post(f"{BASE_URL}/finance", json={}, **_kwargs(auth_token))
        assert resp.status_code == 400
        assert "symbols" in resp.json()["message"] or "code" in resp.json()["message"]

    @pytest.mark.timeout(10)
    def test_finance_rejects_unknown_category(self, auth_token):
        resp = requests.post(
            f"{BASE_URL}/finance",
            json={"symbols": "600000.SH", "category": "no_such_category"},
            **_kwargs(auth_token),
        )
        assert resp.status_code == 400
        assert "Unknown category" in resp.json()["message"]

    @pytest.mark.timeout(10)
    def test_margin_rejects_invalid_json(self, auth_token):
        resp = requests.post(f"{BASE_URL}/margin", data="{bad json",
                            **_kwargs(auth_token))
        assert resp.status_code == 400

    @pytest.mark.timeout(10)
    def test_margin_rejects_bad_date(self, auth_token):
        resp = requests.post(f"{BASE_URL}/margin", json={"date": "202611"},
                            **_kwargs(auth_token))
        assert resp.status_code == 400
        assert "date" in resp.json()["message"]

    @pytest.mark.timeout(10)
    def test_dividend_rejects_invalid_json(self, auth_token):
        resp = requests.post(f"{BASE_URL}/dividend", data="{bad json",
                            **_kwargs(auth_token))
        assert resp.status_code == 400

    @pytest.mark.timeout(10)
    def test_finance_data_requires_csv_lines(self, auth_token):
        resp = requests.post(
            f"{BASE_URL}/finance/data",
            json={"action": "import", "category": "operation"},
            **_kwargs(auth_token),
        )
        assert resp.status_code == 400
        assert "data" in resp.json()["error"]


# ═══════════════════════════════════════════════════════════
#  3. 基本面导入链路：列名映射 / 分批 / 坏行隔离
# ═══════════════════════════════════════════════════════════

# 每个 case: (类目, CSV 列名（baostock 真实表头）, 值, 期望落到的 DB 列)
# 这四组正是历史上永久 NULL 的 12 列所在位置。
FINANCE_COLUMN_CASES = [
    pytest.param(
        "operation",
        ["NRTurnRatio", "NRTurnDays", "INVTurnRatio", "INVTurnDays",
         "CATurnRatio", "AssetTurnRatio"],
        [1.1, 2.2, 3.3, 4.4, 5.5, 6.6],
        ["nr_turn_ratio", "nr_turn_days", "inv_turn_ratio", "inv_turn_days",
         "ca_turn_ratio", "as_turn_ratio"],
        id="operation",
    ),
    pytest.param(
        "balance",
        ["currentRatio", "quickRatio", "cashRatio", "YOYLiability",
         "liabilityToAsset", "assetToEquity"],
        [0.11, 0.22, 0.33, 0.44, 0.55, 0.66],
        ["current_ratio", "quick_ratio", "cash_ratio", "yoy_liability",
         "debt_to_asset", "asset_to_equity"],
        id="balance",
    ),
    pytest.param(
        "cashflow",
        ["CAToAsset", "NCAToAsset", "tangibleAssetToAsset", "ebitToInterest",
         "CFOToOR", "CFOToNP", "CFOToGr"],
        [0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7],
        ["ca_to_asset", "nca_to_asset", "tangible_asset_to_asset",
         "ebit_to_interest", "cfo_to_or", "cfo_to_np", "cfo_to_gr"],
        id="cashflow",
    ),
    pytest.param(
        "dupont",
        ["dupontROE", "dupontAssetStoEquity", "dupontAssetTurn", "dupontPnitoni",
         "dupontNitogr", "dupontTaxBurden", "dupontIntburden", "dupontEbittogr"],
        [0.11, 0.22, 0.33, 0.44, 0.55, 0.66, 0.77, 0.88],
        ["dupont_roe", "dupont_asset_sto_equity", "dupont_asset_turn",
         "dupont_pnitoni", "dupont_nitogr", "dupont_tax_burden",
         "dupont_intburden", "dupont_ebittogr"],
        id="dupont",
    ),
]


class TestFundamentalImportPipeline:
    """FinanceDB::importCsv: baostock 列名映射 + 分块写库"""

    @pytest.mark.parametrize("category,csv_columns,values,db_columns", FINANCE_COLUMN_CASES)
    @pytest.mark.timeout(20)
    def test_baostock_headers_land_in_non_null_columns(
            self, auth_token, category, csv_columns, values, db_columns):
        """CSV 用 baostock 表头导入 → 对应 DB 列必须非空

        旧实现声明的是 ASTurnRatio / debtToAsset / netCashFlowAct 这类
        不存在的列名，importCsv 精确匹配不上，整列静默 NULL。
        """
        symbol = _pick_empty_symbol(auth_token, category)
        try:
            header = "code,pubDate,statDate," + ",".join(csv_columns)
            row = ",".join([symbol, "2024-04-30", "2024-03-31"] +
                           [str(v) for v in values])
            result = _finance_import(auth_token, category, symbol, [header, row])
            assert result["imported_rows"] == 1

            data = _finance_query(auth_token, category, symbol)
            assert data["count"] == 1, f"{category}: 导入 1 行后查不到数据"
            got = data["data"][0]

            for db_col, expected in zip(db_columns, values):
                actual = got.get(db_col)
                assert actual is not None, (
                    f"{category}.{db_col} 为 NULL —— CSV 列名与 baostock 表头不一致 "
                    f"(CSV 列: {csv_columns})"
                )
                assert actual == pytest.approx(expected), \
                    f"{category}.{db_col}: {actual} != {expected}"
        finally:
            _finance_delete(auth_token, category, symbol)

    @pytest.mark.timeout(60)
    def test_import_more_than_one_chunk(self, auth_token):
        """1201 行一次导入 → 分 3 块（500/500/201）且全部可查

        两融每天约 3750 行，旧实现拼单条 INSERT 会踩 DuckDB ART 索引损坏；
        这里用类目表覆盖分块路径，返回行数必须是解析到的有效行数。
        """
        category = "operation"
        symbol = _pick_empty_symbol(auth_token, category)
        header = ("code,pubDate,statDate,NRTurnRatio,NRTurnDays,INVTurnRatio,"
                  "INVTurnDays,CATurnRatio,AssetTurnRatio")
        total = 1201
        start = date(2020, 1, 1)

        lines = [header]
        for i in range(total):
            d = (start + timedelta(days=i)).isoformat()
            lines.append(f"{symbol},{d},{d},1.1,2.2,3.3,4.4,5.5,6.6")

        try:
            result = _finance_import(auth_token, category, symbol, lines)
            assert result["imported_rows"] == total

            data = _finance_query(auth_token, category, symbol, limit=total + 100)
            assert data["count"] == total
            assert data["data"][0]["as_turn_ratio"] == pytest.approx(6.6)
        finally:
            _finance_delete(auth_token, category, symbol)

    @pytest.mark.timeout(30)
    def test_bad_stat_date_row_is_skipped_not_fatal(self, auth_token):
        """一行坏 statDate 只跳过该行，不能把整批打回

        旧行为下一行格式错误就会让 DuckDB 类型转换失败、整批回滚。
        """
        category = "operation"
        symbol = _pick_empty_symbol(auth_token, category)
        header = ("code,pubDate,statDate,NRTurnRatio,NRTurnDays,INVTurnRatio,"
                  "INVTurnDays,CATurnRatio,AssetTurnRatio")
        lines = [
            header,
            f"{symbol},2024-04-30,2024-03-31,1.1,2.2,3.3,4.4,5.5,6.6",
            # 不是 YYYY-MM-DD：importCsv 必须跳过它，而不是整批失败
            f"{symbol},2024-04-30,20240331,1.1,2.2,3.3,4.4,5.5,6.6",
            f"{symbol},2024-08-30,2024-06-30,1.1,2.2,3.3,4.4,5.5,6.6",
        ]

        try:
            result = _finance_import(auth_token, category, symbol, lines)
            assert result["imported_rows"] == 2

            data = _finance_query(auth_token, category, symbol)
            assert data["count"] == 2
        finally:
            _finance_delete(auth_token, category, symbol)


# ═══════════════════════════════════════════════════════════
#  3b. 停牌标记：QuoteData::Check / countInvalidBars / recalc 的口径
# ═══════════════════════════════════════════════════════════

class TestQuoteSuspensionMarker:
    """停牌 bar 的判定口径：volume == 0 或四价全 0 → ext bit0

    QuoteData::Check() 的 OHLC 体检、QuoteDB::countInvalidBars()、
    以及 FinanceDB::recalcSymbolAdjPrices() 都按这个标记跳过停牌 bar。
    """

    SYMBOL = "sh.600009"   # 真实标的；1990 年的假 bar 用精确删除，不影响真实历史
    DATE_SUSPENDED = "1990-01-02"
    DATE_NORMAL = "1990-01-03"

    def _delete(self, auth_token, date):
        requests.delete(
            f"{BASE_URL}/quote",
            params={"table": "stock_1d", "symbol": self.SYMBOL, "date": date},
            **_kwargs(auth_token),
        )

    @pytest.mark.timeout(20)
    def test_zero_volume_bar_is_marked_suspended(self, auth_token):
        lines = [
            "datetime,open,close,high,low,volume,turnover",
            # 停牌日：没有成交量（BaoStock 停牌日的典型形态）
            f"{self.DATE_SUSPENDED} 00:00:00,10.00,10.50,10.60,9.90,0,0",
            # 正常交易日：必须有成交
            f"{self.DATE_NORMAL} 00:00:00,10.00,10.50,10.60,9.90,100000,1050000",
        ]

        self._delete(auth_token, self.DATE_SUSPENDED)
        self._delete(auth_token, self.DATE_NORMAL)
        try:
            result = check_response(requests.post(
                f"{BASE_URL}/quote/data",
                json={
                    "action": "import",
                    "table": "stock_1d",
                    "symbol": self.SYMBOL,
                    # 同一份数据同时作为原始价与复权价（data_hfq 缺省复用 data）
                    "data": lines,
                },
                **_kwargs(auth_token),
            ))
            assert result["imported_rows"] == 2

            data = check_response(requests.get(
                f"{BASE_URL}/quote",
                params={
                    "table": "stock_1d", "symbol": self.SYMBOL,
                    "start": self.DATE_SUSPENDED, "end": self.DATE_NORMAL,
                },
                **_kwargs(auth_token),
            ))
            assert data["count"] == 2
            bars = {bar["datetime"][:10]: bar for bar in data["data"]}
            assert bars[self.DATE_SUSPENDED]["suspended"] is True, \
                "volume == 0 的 bar 必须被标记为停牌"
            assert bars[self.DATE_NORMAL]["suspended"] is False, \
                "有成交量的 bar 不能被误标为停牌"
        finally:
            self._delete(auth_token, self.DATE_SUSPENDED)
            self._delete(auth_token, self.DATE_NORMAL)


# ═══════════════════════════════════════════════════════════
#  3c. 两融导入链路：margin_daily 表结构 + 概览
# ═══════════════════════════════════════════════════════════

class TestMarginImportPipeline:
    """MarginData 的目标表：列齐、可查询、概览可用"""

    MARGIN_DATE = "1990-01-02"   # 远早于真实数据，避免误删
    SYMBOLS = ["sh.600000", "sz.000001"]

    def _cleanup(self, auth_token):
        requests.delete(
            f"{BASE_URL}/margin",
            params={"date": self.MARGIN_DATE},
            **_kwargs(auth_token),
        )

    @pytest.mark.timeout(30)
    def test_margin_csv_import_and_overview(self, auth_token):
        header = ("code,pubDate,statDate,rzBalance,rzBuy,rzRepay,"
                  "rqVolume,rqSell,rqRepay,rqBalance,totalBalance")
        rows = [
            f"{self.SYMBOLS[0]},1990-01-03,1990-01-02,100.0,10.0,5.0,20,2,1,30.0,130.0",
            f"{self.SYMBOLS[1]},1990-01-03,1990-01-02,200.0,20.0,10.0,40,4,2,60.0,260.0",
        ]

        self._cleanup(auth_token)
        try:
            result = _finance_import(auth_token, "margin_daily", "", [header] + rows)
            assert result["imported_rows"] == 2

            detail = check_response(requests.get(
                f"{BASE_URL}/margin",
                params={"date": self.MARGIN_DATE},
                **_kwargs(auth_token),
            ))
            assert detail["count"] == 2
            by_code = {r["code"]: r for r in detail["data"]}
            assert set(by_code) == set(self.SYMBOLS)
            for code, row in by_code.items():
                assert row["stat_date"].startswith(self.MARGIN_DATE)
                assert row["pub_date"].startswith("1990-01-03")
                assert row["rz_balance"] is not None, f"{code} rz_balance 为 NULL"
                assert row["total_balance"] is not None, f"{code} total_balance 为 NULL"

            # MarginData 的 DB 级体检用的就是 queryCategoryOverview，
            # 这里顺带锁住它的返回结构
            overview = check_response(requests.get(
                f"{BASE_URL}/margin", params={"days": 5}, **_kwargs(auth_token)))
            assert overview["table"] == "margin_daily"
            assert overview["rows"] >= 2
            assert overview["symbols"] >= 2
            assert "start_date" in overview and "latest_date" in overview
            assert isinstance(overview["recent"], list)
        finally:
            self._cleanup(auth_token)


# ═══════════════════════════════════════════════════════════
#  3d. 分红导入作用域：目录里的池外 CSV 不得入库
# ═══════════════════════════════════════════════════════════

class TestDividendImportScope:
    """DividendHandler action=import 的 symbols 过滤

    用真实标的 + 1990 年的假除权日，导入/删除都只碰这一条，不影响真实分红历史。
    """

    IN_SCOPE = "600000.SH"
    OUT_OF_SCOPE = "600519.SH"
    EX_DATE = "1990-01-04"

    DIVIDEND_HEADER = [
        "symbol", "announce_date", "report_year", "ex_dividend_date",
        "record_date", "implement_date", "bonus_per_10", "transfer_per_10",
        "cash_per_10", "allot_per_10", "allot_price", "ex_div_price", "action_type",
    ]

    @classmethod
    def _write_csv(cls, directory, symbol):
        row = [symbol, "1990-01-03", "1989", cls.EX_DATE, "1990-01-03",
               cls.EX_DATE, "0", "0", "2.0", "0", "0", "10.30", "1"]
        path = directory / f"{symbol}_dividend.csv"
        import csv

        with open(path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(cls.DIVIDEND_HEADER)
            writer.writerow(row)

    @staticmethod
    def _delete(auth_token, code):
        # 只删 1990-01-04 这一条，真实分红事件不动
        requests.delete(
            f"{BASE_URL}/dividend",
            params={"code": code, "ex_date": "1990-01-04"},
            **_kwargs(auth_token),
        )

    def _dividend_of(self, auth_token, code):
        return check_response(requests.get(
            f"{BASE_URL}/dividend",
            params={"code": code, "start": "1990-01-01", "end": "1990-12-31"},
            **_kwargs(auth_token),
        ))

    @pytest.mark.timeout(30)
    def test_import_only_requested_symbols(self, auth_token, tmp_path):
        """目录里放两个文件，scope 只点一个 → 只导入 1 行

        目录可能躺着别的任务留下的 CSV；全量导入会把池外标的的分红事件
        写进 dividend 表，后续复权重算会按错误事件算因子。
        """
        in_scope = "sh.600000"
        out_scope = "sh.600519"
        self._delete(auth_token, in_scope)
        self._delete(auth_token, out_scope)
        self._write_csv(tmp_path, self.IN_SCOPE)
        self._write_csv(tmp_path, self.OUT_OF_SCOPE)

        try:
            result = check_response(requests.post(
                f"{BASE_URL}/dividend",
                json={
                    "action": "import",
                    "dividend_dir": str(tmp_path),
                    "symbols": self.IN_SCOPE,
                },
                **_kwargs(auth_token),
            ))
            assert result["status"] == "completed"
            assert result["imported_rows"] == 1, "scope 之外的 *_dividend.csv 不应入库"
            assert result["failed_files"] == 0

            got = self._dividend_of(auth_token, in_scope)
            assert got["count"] == 1
            assert got["data"][0]["cash_per_10"] == pytest.approx(2.0)

            out = self._dividend_of(auth_token, out_scope)
            assert out["count"] == 0, "池外标的的分红事件被导入了"
        finally:
            self._delete(auth_token, in_scope)
            self._delete(auth_token, out_scope)


# ═══════════════════════════════════════════════════════════
#  4. 真实 pipeline（网络，默认 skip）
# ═══════════════════════════════════════════════════════════

@needs_network
class TestRealPipelineRuns:
    """通过 HTTP 触发真实 pipeline：真实标的 + 有限窗口

    都不下载全历史：
      - 行情：只对库里已有数据的标的触发，Download 会从库内最新日期增量补
      - 基本面：窗口限定 2024-Q1~Q4（Check 要求至少 4 个季度）
      - 两融：只抓库里已有的一个交易日
    """

    @pytest.mark.timeout(420)
    def test_finance_pipeline_downloads_only_requested_quarters(self, auth_token):
        """POST /finance 限定 2024 全年 → 只入库 2024 四个季度的报告期"""
        category = "operation"
        symbol = "600519.SH"
        sse = _open_sse(auth_token)
        try:
            resp = requests.post(
                f"{BASE_URL}/finance",
                json={"symbols": symbol, "category": category,
                      "start": "2024-Q1", "end": "2024-Q4"},
                **_kwargs(auth_token),
            )
            assert resp.status_code == 200
            assert resp.json()["status"] == "started"

            event = _wait_sse(sse, "fundamental_refresh", timeout=270,
                              predicate=lambda d: d.get("status") in ("completed", "error"))
            assert event["status"] == "completed", f"基本面刷新失败: {event}"
            assert int(event.get("imported", "0")) > 0, event
        finally:
            sse.close()

        # SSE 的 fundamental_refresh 与每日调度器同名：即便等到了（或没等到）
        # 也不以事件为准，最终以库里真的出现 2024 四个季度为准。
        _wait_until(
            lambda: (_finance_query(auth_token, category, "sh.600519",
                                    start="2024-01-01", end="2024-12-31")
                     .get("count") == 4),
            120, message="2024 四个季度入库",
        )
        data = _finance_query(auth_token, category, "sh.600519",
                              start="2024-01-01", end="2024-12-31")
        assert data["count"] == 4, f"2024 四个季度应各一条，实际 {data['count']}"
        stat_dates = {row["stat_date"][:10] for row in data["data"]}
        assert stat_dates == {"2024-03-31", "2024-06-30", "2024-09-30", "2024-12-31"}, \
            f"窗口外的报告期被拉进来了: {stat_dates}"
        # 列名映射错了会是整列 NULL
        assert any(row["as_turn_ratio"] is not None for row in data["data"]), \
            "operation.as_turn_ratio 全部为 NULL，CSV 列名映射可能回退了"

    @pytest.mark.timeout(240)
    def test_quote_pipeline_incremental_on_existing_symbol(self, auth_token):
        """POST /quote 对库里已有标的做增量补齐，不重下全历史"""
        tables = check_response(requests.get(f"{BASE_URL}/quote", **_kwargs(auth_token)))
        assert isinstance(tables, list)

        target = None
        for tbl in tables:
            if tbl.get("table") != "stock_1d":
                continue
            for item in tbl.get("symbols", []):
                sym = item.get("symbol", "")
                # 真实股票代码：sh.6xxxxx / sz.0xxxxx（排除 9 开头的测试标的）
                if sym.startswith(("sh.6", "sz.0")) and item.get("count", 0) > 0:
                    target = item
                    break
            if target:
                break

        if not target:
            pytest.skip("stock_1d 里没有可用的真实标的（先导入一份行情再跑）")

        symbol = target["symbol"]          # 内部格式 sh.600000
        external = symbol.split(".")[1] + "." + symbol.split(".")[0].upper()

        sse = _open_sse(auth_token)
        try:
            resp = requests.post(
                f"{BASE_URL}/quote",
                json={"symbols": external, "freq": "daily"},
                **_kwargs(auth_token),
            )
            assert resp.status_code == 200
            assert resp.json()["status"] == "started"

            event = _wait_sse(sse, "quote_download", timeout=210,
                              predicate=lambda d: d.get("freq") == "daily"
                              and d.get("status") in ("done", "failed"))
            assert event["status"] == "done" and event.get("success") == "true", \
                f"行情增量下载失败: {event}"
        finally:
            sse.close()

        # 增量导入后原数据仍在（从库内最新日期往后补）
        data = check_response(requests.get(
            f"{BASE_URL}/quote",
            params={"table": "stock_1d", "symbol": symbol, "limit": 1},
            **_kwargs(auth_token),
        ))
        assert data["count"] > 0, f"{symbol} 增量刷新后查不到数据"

    @pytest.mark.timeout(400)
    def test_margin_pipeline_single_trading_day(self, auth_token):
        """POST /margin 只抓一个交易日（库里已有的最近日期）"""
        overview = check_response(requests.get(
            f"{BASE_URL}/margin", params={"days": 5}, **_kwargs(auth_token)))
        target = overview.get("latest_date") or ""
        if not target:
            # 库里没有两融数据时退回到最近一个工作日
            day = datetime.now().date() - timedelta(days=1)
            while day.weekday() >= 5:
                day -= timedelta(days=1)
            target = day.isoformat()

        sse = _open_sse(auth_token)
        try:
            resp = requests.post(f"{BASE_URL}/margin", json={"date": target},
                                 **_kwargs(auth_token))
            assert resp.status_code == 200
            assert resp.json()["status"] == "started"

            event = _wait_sse(sse, "margin_download", timeout=330,
                              predicate=lambda d: d.get("status") in ("completed", "failed"))
            assert event["status"] == "completed", f"两融下载失败: {event}"
            assert int(event.get("rows", "0")) > 0, event
        finally:
            sse.close()

        detail = check_response(requests.get(
            f"{BASE_URL}/margin", params={"date": target}, **_kwargs(auth_token)))
        assert detail["count"] > 0
        for row in detail["data"]:
            assert row["pub_date"][:10] == _next_day(row["stat_date"][:10]), \
                f"pubDate 必须是 statDate+1: {row}"
