#!/usr/bin/env python3
"""
节点输入输出日志系统测试

测试方案：
1. 验证 node_io_logs 表创建和写入
2. 验证查询 API（GET /v0/node/io）
3. 验证清理 API（DELETE /v0/node/io）
4. 验证不同 node_type 过滤

使用方法：
  pytest test_node_io_logging.py -v
  pytest test_node_io_logging.py::TestNodeIOAPI::test_query_empty -v

前置准备：
  - 服务已启动（QuantService config.json）
  - DuckDB 日志已初始化
"""

import pytest
import requests
import json
import time
from pathlib import Path
from typing import Dict, List, Optional

# 抑制 SSL 警告
import urllib3
urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)


# ============================================================
# 配置
# ============================================================

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False


# ============================================================
# Fixture
# ============================================================

@pytest.fixture(scope="session")
def auth_token():
    """获取 JWT token"""
    try:
        resp = requests.post(
            f"{BASE_URL}/user/login",
            json={"username": "admin", "password": "admin123"},
            verify=VERIFY_SSL
        )
        if resp.status_code == 200:
            return resp.json()["token"]
    except Exception:
        pass
    return ""


@pytest.fixture
def headers(auth_token):
    """带认证的请求头"""
    h = {}
    if auth_token:
        h["Authorization"] = f"Bearer {auth_token}"
    return h


# ============================================================
# 测试：查询 API
# ============================================================

class TestNodeIOAPI:
    """测试 GET /v0/node/io 查询接口"""

    def test_query_empty(self, headers):
        """空数据查询"""
        resp = requests.get(
            f"{BASE_URL}/node/io",
            headers=headers,
            verify=VERIFY_SSL
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "total" in data
        assert "logs" in data
        assert isinstance(data["logs"], list)

    def test_query_with_strategy(self, headers):
        """按策略名称查询"""
        resp = requests.get(
            f"{BASE_URL}/node/io?strategy=test_strategy&limit=10",
            headers=headers,
            verify=VERIFY_SSL
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "total" in data

    def test_query_with_node_type(self, headers):
        """按节点类型查询"""
        for node_type in ["input", "signal", "portfolio", "execution"]:
            resp = requests.get(
                f"{BASE_URL}/node/io?node_type={node_type}&limit=10",
                headers=headers,
                verify=VERIFY_SSL
            )
            assert resp.status_code == 200
            data = resp.json()
            # 验证返回的日志 node_type 匹配
            for log in data.get("logs", []):
                if "node_type" in log:
                    assert log["node_type"] == node_type

    def test_query_pagination(self, headers):
        """分页查询"""
        # 第一页
        resp1 = requests.get(
            f"{BASE_URL}/node/io?limit=5&offset=0",
            headers=headers,
            verify=VERIFY_SSL
        )
        assert resp1.status_code == 200
        data1 = resp1.json()

        # 第二页
        resp2 = requests.get(
            f"{BASE_URL}/node/io?limit=5&offset=5",
            headers=headers,
            verify=VERIFY_SSL
        )
        assert resp2.status_code == 200
        data2 = resp2.json()

        # 两页数据不应重叠（如果有数据）
        if data1["logs"] and data2["logs"]:
            ids1 = {log["id"] for log in data1["logs"]}
            ids2 = {log["id"] for log in data2["logs"]}
            assert len(ids1 & ids2) == 0  # 无交集


# ============================================================
# 测试：清理 API
# ============================================================

class TestNodeIOCleanup:
    """测试 DELETE /v0/node/io 清理接口"""

    def test_delete_requires_date(self, headers):
        """缺少 before_date 参数应返回 400"""
        resp = requests.delete(
            f"{BASE_URL}/node/io",
            headers=headers,
            verify=VERIFY_SSL
        )
        assert resp.status_code == 400
        data = resp.json()
        assert "error" in data

    def test_delete_old_data(self, headers):
        """清理旧数据"""
        # 使用过去的日期，应该不会删除任何数据（或返回 0）
        resp = requests.delete(
            f"{BASE_URL}/node/io?before_date=2020-01-01",
            headers=headers,
            verify=VERIFY_SSL
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "deleted_count" in data
        assert "before_date" in data


# ============================================================
# 测试：数据结构验证
# ============================================================

class TestNodeIOStructure:
    """验证返回数据的结构"""

    def _get_sample_log(self, headers) -> Optional[Dict]:
        """获取一条样本日志"""
        resp = requests.get(
            f"{BASE_URL}/node/io?limit=1",
            headers=headers,
            verify=VERIFY_SSL
        )
        if resp.status_code == 200:
            data = resp.json()
            if data.get("logs"):
                return data["logs"][0]
        return None

    def test_log_has_required_fields(self, headers):
        """日志条目包含必需字段"""
        log = self._get_sample_log(headers)
        if log is None:
            pytest.skip("No node IO logs available")

        required_fields = ["id", "timestamp", "strategy", "epoch", "node_type"]
        for field in required_fields:
            assert field in log, f"Missing field: {field}"

    def test_node_type_values(self, headers):
        """node_type 值应为预定义字符串"""
        valid_types = {"input", "signal", "portfolio", "execution", "function", "debug"}
        resp = requests.get(
            f"{BASE_URL}/node/io?limit=100",
            headers=headers,
            verify=VERIFY_SSL
        )
        if resp.status_code == 200:
            data = resp.json()
            for log in data.get("logs", []):
                assert log.get("node_type") in valid_types, f"Invalid node_type: {log.get('node_type')}"

    def test_json_fields_parseable(self, headers):
        """input/output/metadata 字段应为可解析的 JSON"""
        resp = requests.get(
            f"{BASE_URL}/node/io?limit=10",
            headers=headers,
            verify=VERIFY_SSL
        )
        if resp.status_code == 200:
            data = resp.json()
            for log in data.get("logs", []):
                for field in ["input", "output", "metadata"]:
                    if field in log:
                        # 应该是已解析的 JSON 对象或字符串
                        assert isinstance(log[field], (dict, list, str))


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
