#!/usr/bin/env python3
"""
策略稳定性扫描 API 冒烟测试

测试 /v0/analysis/stability 端点的完整流程：
1. POST action=scan 提交扫描任务
2. GET action=status 查询状态
3. GET action=result 获取结果
4. GET action=list 列出历史
5. POST action=delete 删除结果

运行：
  pytest service/test/testcases/test_stability_api.py -v
"""

import json
import time
from pathlib import Path

import numpy as np
import pandas as pd
import pytest
import requests


def make_test_csv(path: Path, n_days=200, n_syms=10, seed=7):
    """生成测试用宽表 CSV"""
    rng = np.random.default_rng(seed)
    dates = pd.bdate_range("2023-01-02", periods=n_days)
    symbols = [f"sh.{600000 + i}" for i in range(n_syms)]

    data = {}
    for sym in symbols:
        sig = rng.normal(0, 1, n_days)
        ret = 0.15 * sig + rng.normal(0, 0.005, n_days)
        close = 100.0 * np.exp(np.cumsum(ret))
        data[f"{sym}.close"] = close
        data[f"{sym}.raw_strength"] = sig
        data[f"{sym}.feat_a"] = 0.15 * sig + rng.normal(0, 0.005, n_days)

    df = pd.DataFrame(data, index=dates)
    df.index.name = "datetime"
    df.to_csv(path)
    return path


class TestStabilityAPI:
    """策略稳定性扫描 API 测试"""

    @pytest.fixture
    def base_url(self):
        """服务地址（从环境变量或默认值）"""
        import os
        return os.environ.get("QS_TEST_URL", "http://localhost:19107")

    @pytest.fixture
    def auth_headers(self):
        """认证头（从环境变量或默认值）"""
        import os
        token = os.environ.get("QS_TEST_TOKEN", "")
        if token:
            return {"Authorization": f"Bearer {token}"}
        return {}

    @pytest.fixture
    def test_csv(self, tmp_path):
        """生成测试 CSV"""
        csv_path = tmp_path / "test_stability.csv"
        make_test_csv(csv_path)
        return str(csv_path)

    def test_scan_and_wait(self, base_url, auth_headers, test_csv):
        """提交扫描并等待完成"""
        # 1. 提交扫描
        resp = requests.post(
            f"{base_url}/v0/analysis/stability",
            json={
                "action": "scan",
                "csv_path": test_csv,
                "strategy_id": "test_api",
                "checks": ["drift", "block"],
            },
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 200, f"scan failed: {resp.text}"
        data = resp.json()
        assert "session_id" in data
        assert "run_id" in data
        session_id = data["session_id"]
        run_id = data["run_id"]

        # 2. 轮询状态
        for _ in range(60):  # 最多等 60 秒
            resp = requests.get(
                f"{base_url}/v0/analysis/stability",
                params={"action": "status", "session_id": session_id},
                headers=auth_headers,
                timeout=10,
            )
            assert resp.status_code == 200
            status_data = resp.json()
            if status_data.get("status") in ("done", "error"):
                break
            time.sleep(1)

        assert status_data["status"] == "done", f"scan failed: {status_data}"

        # 3. 获取结果
        resp = requests.get(
            f"{base_url}/v0/analysis/stability",
            params={"action": "result", "run_id": run_id},
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 200
        result = resp.json()
        assert result["schema"] == "stability/v1"
        assert "meta" in result
        assert "summary" in result
        assert "drift" in result

        # 4. 列出历史
        resp = requests.get(
            f"{base_url}/v0/analysis/stability",
            params={"action": "list"},
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 200
        items = resp.json()
        assert isinstance(items, list)
        assert any(item["run_id"] == run_id for item in items)

        # 5. 删除结果
        resp = requests.post(
            f"{base_url}/v0/analysis/stability",
            json={"action": "delete", "run_id": run_id},
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 200
        assert resp.json()["message"] == "deleted"

    def test_invalid_csv_path(self, base_url, auth_headers):
        """测试无效的 CSV 路径"""
        resp = requests.post(
            f"{base_url}/v0/analysis/stability",
            json={
                "action": "scan",
                "csv_path": "/nonexistent/path.csv",
            },
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 400
        assert "not found" in resp.json()["message"].lower()

    def test_missing_csv_path(self, base_url, auth_headers):
        """测试缺少 CSV 路径"""
        resp = requests.post(
            f"{base_url}/v0/analysis/stability",
            json={"action": "scan"},
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 400
        assert "missing" in resp.json()["message"].lower()

    def test_invalid_action(self, base_url, auth_headers):
        """测试无效的操作"""
        resp = requests.post(
            f"{base_url}/v0/analysis/stability",
            json={"action": "invalid"},
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 400

    def test_invalid_run_id_format(self, base_url, auth_headers):
        """测试无效的 run_id 格式"""
        resp = requests.get(
            f"{base_url}/v0/analysis/stability",
            params={"action": "result", "run_id": "invalid_format"},
            headers=auth_headers,
            timeout=10,
        )
        assert resp.status_code == 400
        assert "invalid" in resp.json()["message"].lower()
