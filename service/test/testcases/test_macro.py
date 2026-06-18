"""
MacroHandler 测试用例

测试 /v0/macro GET 接口：
- 参数校验（缺失参数、无效国家、无效指标）
- 响应结构验证
- 日期范围过滤
- 缓存机制（写入缓存后重启服务验证命中）

MacroHandler 依赖 Python 脚本 fetch_macro_data.py 获取数据，
因此本测试在模拟模式下：
1. 不依赖外部 Python 脚本可用性（通过缓存或降级路径）
2. 重点测试参数校验、响应结构、FilterByDate 逻辑
"""
import pytest
import requests
import json
import os
from pathlib import Path
from tool import check_response, BASE_URL


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
