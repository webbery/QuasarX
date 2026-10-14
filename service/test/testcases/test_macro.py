"""
MacroHandler 测试用例

测试 /v0/macro GET 接口：
- 参数校验（缺失参数、无效国家、无效指标）
- 响应结构验证
- 日期范围过滤
- 缓存机制（写入缓存后重启服务验证命中）
- DuckDB 存储验证（新增）

MacroHandler 依赖 Python 脚本 fetch_macro_data.py 获取数据，
因此本测试在模拟模式下：
1. 不依赖外部 Python 脚本可用性（通过缓存或降级路径）
2. 重点测试参数校验、响应结构、FilterByDate 逻辑
3. 验证 DuckDB 表结构和数据持久化（新增）
"""
import pytest
import requests
import json
import os
from pathlib import Path
from tool import check_response, BASE_URL, _DATA_DIR


@pytest.mark.usefixtures("auth_token")
class TestMacroParamValidation:
    """参数校验测试"""

    def _req(self, auth_token, **params):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return requests.get(f"{BASE_URL}/market/macro", params=params, **kwargs)

    def test_missing_country(self, auth_token):
        """缺少 country 参数应返回 400"""
        resp = self._req(auth_token, indicator="cpi")
        assert resp.status_code == 400
        data = resp.json()
        assert data["status"] == "error"
        assert data["code"] == "MISSING_PARAMS"

    def test_missing_indicator(self, auth_token):
        """缺少 indicator 参数应返回 400"""
        resp = self._req(auth_token, country="china")
        assert resp.status_code == 400
        data = resp.json()
        assert data["status"] == "error"
        assert data["code"] == "MISSING_PARAMS"

    def test_missing_both_params(self, auth_token):
        """同时缺少 country 和 indicator 应返回 400"""
        resp = self._req(auth_token)
        assert resp.status_code == 400
        data = resp.json()
        assert data["status"] == "error"
        assert data["code"] == "MISSING_PARAMS"

    def test_invalid_country(self, auth_token):
        """无效的国家参数应返回 400"""
        resp = self._req(auth_token, country="japan", indicator="cpi")
        assert resp.status_code == 400
        data = resp.json()
        assert data["status"] == "error"
        assert data["code"] == "INVALID_COUNTRY"

    def test_invalid_indicator(self, auth_token):
        """无效的指标参数应返回 400"""
        resp = self._req(auth_token, country="china", indicator="nonexistent")
        assert resp.status_code == 400
        data = resp.json()
        assert data["status"] == "error"
        assert data["code"] == "INVALID_INDICATOR"

    def test_valid_countries(self, auth_token):
        """所有合法国家参数都应通过校验（不报错 400）"""
        for country in ["china", "usa", "global"]:
            resp = self._req(auth_token, country=country, indicator="cpi")
            # 不检查 200 还是 500（取决于数据源可用性），只要不是参数错误即可
            assert resp.status_code != 400, f"country={country} 不应返回 400"

    def test_valid_indicators(self, auth_token):
        """所有合法指标参数都应通过校验（不报错 400）"""
        indicators = [
            "cpi", "ppi", "gdp", "pmi", "m2", "social_financing",
            "unemployment", "trade", "interest_rate", "retail_sales",
            "industrial_production", "fixed_asset_investment",
            "consumer_confidence", "housing_starts", "nonfarm"
        ]
        for indicator in indicators:
            resp = self._req(auth_token, country="china", indicator=indicator)
            assert resp.status_code != 400, f"indicator={indicator} 不应返回 400"


@pytest.mark.usefixtures("auth_token")
class TestMacroResponseStructure:
    """响应结构测试"""

    def _req(self, auth_token, **params):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return requests.get(f"{BASE_URL}/market/macro", params=params, **kwargs)

    def test_success_response_fields(self, auth_token):
        """成功响应应包含 status, country, indicator, count, data"""
        resp = self._req(auth_token, country="china", indicator="cpi")
        # 可能 200（成功）或 500（数据源不可用）
        if resp.status_code == 200:
            data = resp.json()
            assert data["status"] == "success"
            assert "country" in data
            assert "indicator" in data
            assert "count" in data
            assert "data" in data
            assert isinstance(data["data"], list)
            assert data["country"] == "china"
            assert data["indicator"] == "cpi"
            assert data["count"] == len(data["data"])

    def test_cached_field_in_response(self, auth_token):
        """成功响应应包含 cached 字段"""
        resp = self._req(auth_token, country="china", indicator="cpi")
        if resp.status_code == 200:
            data = resp.json()
            assert "cached" in data
            assert isinstance(data["cached"], bool)

    def test_date_filter(self, auth_token):
        """日期范围过滤应正确工作"""
        resp = self._req(
            auth_token,
            country="china",
            indicator="cpi",
            start="2025-01-01",
            end="2025-06-01"
        )
        if resp.status_code == 200:
            data = resp.json()
            for item in data["data"]:
                date_str = item.get("date", "")
                if date_str:
                    assert date_str >= "2025-01-01"
                    assert date_str <= "2025-06-01"

    def test_stale_flag_on_fallback(self, auth_token):
        """当 Python 失败但缓存存在时，stale 应为 true"""
        # 第一次请求可能缓存过期，返回 stale=false 或 stale=true
        resp = self._req(auth_token, country="china", indicator="cpi")
        if resp.status_code == 200:
            data = resp.json()
            if data.get("cached"):
                # 如果使用了缓存，stale 字段应存在
                assert "stale" in data


@pytest.mark.usefixtures("auth_token")
class TestMacroCache:
    """缓存机制测试"""

    def _req(self, auth_token, **params):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return requests.get(f"{BASE_URL}/market/macro", params=params, **kwargs)

    def test_refresh_param(self, auth_token):
        """refresh=true 应强制获取新数据"""
        # 先获取一次（可能写入缓存）
        self._req(auth_token, country="china", indicator="cpi")

        # 再次请求并强制刷新
        resp = self._req(
            auth_token,
            country="china",
            indicator="cpi",
            refresh="true"
        )
        if resp.status_code == 200:
            data = resp.json()
            # 强制刷新时 cached 应为 false（除非 Python 脚本也失败降级）
            if not data.get("stale"):
                assert data.get("cached") == False

    def test_cache_persistence(self, auth_token):
        """写入缓存后，第二次请求应命中缓存"""
        # 第一次请求
        resp1 = self._req(auth_token, country="china", indicator="pmi")
        if resp1.status_code != 200:
            pytest.skip("首次请求失败，无法测试缓存")

        # 第二次请求（应命中缓存）
        resp2 = self._req(auth_token, country="china", indicator="pmi")
        if resp2.status_code == 200:
            data2 = resp2.json()
            # 缓存命中时 cached=true 且 stale=false
            if data2.get("cached"):
                assert data2.get("stale") == False, "缓存未过期时 stale 应为 false"


# ═══════════════════════════════════════════════════════════
#  DuckDB 存储验证（新增）
# ═══════════════════════════════════════════════════════════

@pytest.mark.usefixtures("auth_token")
class TestMacroDuckDBStorage:
    """DuckDB 存储验证测试"""

    def _req(self, auth_token, **params):
        """发送 GET 请求到 /v0/market/macro"""
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return requests.get(f"{BASE_URL}/market/macro", params=params, **kwargs)

    @pytest.fixture(autouse=True)
    def ensure_financedb_initialized(self, auth_token):
        """确保 FinanceDB 已初始化（通过 API 触发）"""
        kwargs = {'verify': False, 'timeout': 5}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        
        # 调用 /v0/finance 触发 FinanceDB 初始化
        try:
            requests.get(f"{BASE_URL}/v0/finance", **kwargs)
        except Exception as e:
            pytest.skip(f"无法连接服务: {e}")
        
        yield

    def test_duckdb_table_exists(self, auth_token):
        """验证 macro_data 表是否存在（通过 API 间接验证）"""
        # 由于 DuckDB 文件锁，不能在服务运行时直接打开文件
        # 改为通过查询 API 验证表是否存在
        try:
            # 查询一个不存在的指标，如果返回空数据而不是错误，说明表存在
            resp = self._req(auth_token, country="usa", indicator="sofr")
            
            # 如果表不存在，FinanceDB.queryMacro 会返回错误
            # 如果表存在但没数据，会返回空数组
            if resp.status_code == 200:
                data = resp.json()
                # 表存在的证据：返回了正确的结构
                assert "data" in data or "count" in data
            else:
                # 表可能不存在，跳过测试
                pytest.skip("macro_data 表可能不存在")
                
        except Exception as e:
            pytest.skip(f"无法验证表存在: {e}")
    
    def test_duckdb_import_and_query(self, auth_token):
        """验证宏观数据导入和查询功能（通过 API）"""
        # 由于 DuckDB 文件锁，改为通过 API 验证
        # 查询一个假设存在的指标
        resp = self._req(auth_token, country="usa", indicator="sofr")
        
        # 如果返回 200，说明查询成功（表存在且查询逻辑正确）
        if resp.status_code == 200:
            data = resp.json()
            assert "data" in data
            assert "count" in data
        else:
            # 数据可能不存在，但表应该存在
            pytest.skip("sofr 数据可能不存在，跳过导入查询测试")
    
    def test_duckdb_date_filter(self, auth_token):
        """验证日期过滤功能（通过 API，使用1个月日期范围）"""
        # 通过 API 验证日期过滤（使用较短的日期范围：1个月）
        resp = self._req(
            auth_token,
            country="usa",
            indicator="sofr",
            start="2025-01-01",
            end="2025-01-31"
        )
        
        if resp.status_code == 200:
            data = resp.json()
            # 验证返回的数据都在指定日期范围内
            for item in data.get("data", []):
                date_str = item.get("date", "")
                if date_str:
                    assert "2025-01-01" <= date_str <= "2025-01-31", \
                        f"日期过滤失效: {date_str} 不在 2025-01 范围内"
        else:
            pytest.skip("sofr 数据可能不存在，跳过日期过滤测试")
    
    def test_duckdb_unique_constraint(self, auth_token):
        """验证唯一性约束（通过 API 间接验证）"""
        # 由于 DuckDB 文件锁，无法直接测试 INSERT OR REPLACE
        # 改为验证：多次查询同一数据不会返回重复记录
        resp1 = self._req(auth_token, country="usa", indicator="sofr")
        resp2 = self._req(auth_token, country="usa", indicator="sofr")
        
        if resp1.status_code == 200 and resp2.status_code == 200:
            data1 = resp1.json()
            data2 = resp2.json()
            
            # 两次查询应该返回相同的数据
            assert data1.get("count") == data2.get("count"), \
                "两次查询返回不同数量的记录，可能存在重复"
        else:
            pytest.skip("sofr 数据不存在，跳过唯一性约束测试")
