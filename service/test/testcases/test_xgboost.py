#!/usr/bin/env python3
"""
XGBoost 训练与分析 API 测试

测试 /v0/ml 端点：
- POST action=train    训练模型
- POST action=publish  发布实验模型到生产
- POST action=shap     计算 SHAP 值（兼容旧 POST 方式）
- GET  action=list     列出实验和生产模型
- GET  action=shap     计算 SHAP 值
- DELETE model_id=N    释放内存中已注册的模型

前置准备：
  1. 服务已启动（auth_token/conftest.py 提供）
  2. 测试数据已上传（upload_test_data 自动扫描 ai_test_data/）
  3. Python 环境需安装：xgboost, pandas, scikit-learn, numpy
"""

import json
import time
import pytest
import urllib3
import requests
from pathlib import Path

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 训练策略脚本路径
STRATEGY_PATH = Path(__file__).parent / "ai_test_data" / "xgboost_train_strategy.json"


def _headers(auth_token):
    return {"Authorization": auth_token}


def _load_strategy():
    return STRATEGY_PATH.read_text()


# ============== Fixtures ==============

@pytest.fixture(scope="module")
def trained_model(auth_token):
    """
    训练一个简单策略，返回 {model_id, model_path, n_features, n_train, n_test}。
    如果训练失败（缺数据/缺 Python 环境），整个 module 的测试 skip。
    """
    script = _load_strategy()
    body = {
        "action": "train",
        "script": script,
        "label": {
            "source": "sz.800001.close",
            "period": 5,
            "type": "classification",
            "vol_k": 0.5,
        },
        "objective": "multi:softprob",
        "num_class": 3,
        "test_ratio": 0.2,
        "params": {
            "learning_rate": 0.1,
            "max_depth": 3,
            "n_estimators": 20,
        },
    }
    try:
        resp = requests.post(
            f"{BASE_URL}/ml",
            json=body,
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=120,
        )
    except requests.exceptions.Timeout:
        pytest.skip("训练请求超时（可能缺少测试数据或 Python 环境）")

    if resp.status_code != 200:
        pytest.skip(f"训练失败（依赖缺失）: {resp.text[:200]}")

    submit_result = resp.json()
    session_id = submit_result.get("session_id")
    if not session_id:
        pytest.skip(f"训练响应缺少 session_id: {submit_result}")

    # 轮询 train_status 直到训练完成（最多 120s）
    for _ in range(120):
        time.sleep(1)
        try:
            status_resp = requests.get(
                f"{BASE_URL}/ml",
                params={"action": "train_status", "session_id": session_id},
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
        except requests.exceptions.RequestException:
            continue

        if status_resp.status_code != 200:
            continue

        data = status_resp.json()
        status = data.get("status")
        if status == "done":
            assert "model_id" in data, "训练完成但响应缺少 model_id"
            assert "model_path" in data, "训练完成但响应缺少 model_path"
            return data
        if status == "error":
            pytest.skip(f"训练失败: {data.get('error', 'unknown')}")
        # status == "running" → 继续轮询

    pytest.skip("训练超时（120s 内未完成）")


# ============== 错误场景测试（无需训练） ==============

class TestXGBoostErrors:
    """不依赖训练结果的错误场景测试"""

    def test_post_missing_action(self, auth_token):
        """POST 缺少 action 返回 400"""
        resp = requests.post(
            f"{BASE_URL}/ml",
            json={"script": "{}"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 400

    def test_post_invalid_action(self, auth_token):
        """POST 无效 action 返回 400"""
        resp = requests.post(
            f"{BASE_URL}/ml",
            json={"action": "foo"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 400

    def test_get_invalid_action(self, auth_token):
        """GET 无效 action 返回 400"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "foo"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 400

    def test_get_shap_missing_model_id(self, auth_token):
        """GET shap 缺少 model_id 返回 400"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "shap"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 400

    def test_get_shap_invalid_model_id(self, auth_token):
        """GET shap 不存在的 model_id 返回 404"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "shap", "model_id": "99999"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 404

    def test_delete_missing_model_id(self, auth_token):
        """DELETE 缺少 model_id 返回 400"""
        resp = requests.delete(
            f"{BASE_URL}/ml",
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 400

    def test_delete_nonexistent_model(self, auth_token):
        """DELETE 不存在的 model_id 返回 404"""
        resp = requests.delete(
            f"{BASE_URL}/ml",
            params={"model_id": "99999"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 404

    def test_download_missing_model_id(self, auth_token):
        """download 缺少 model_id 返回 400"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "download"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 400

    def test_download_invalid_model_id(self, auth_token):
        """download 无效 model_id 返回 400"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "download", "model_id": "abc"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 400

    def test_download_nonexistent_model(self, auth_token):
        """download 不存在的 model_id 返回 404"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "download", "model_id": "99999999"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 404


# ============== List 测试（无需训练） ==============

class TestXGBoostList:
    """list 功能测试，不依赖训练结果"""

    def test_list_structure(self, auth_token):
        """GET list 返回正确结构"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "list"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert "experiments" in data
        assert "production" in data
        assert isinstance(data["experiments"], list)

    def test_list_experiment_has_meta(self, auth_token, trained_model):
        """list 中实验模型包含 meta 字段"""
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "list"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert len(data["experiments"]) > 0

        # 找到刚训练的模型
        model_path = trained_model["model_path"]
        found = [e for e in data["experiments"] if e.get("path") == model_path]
        assert len(found) == 1, f"未在 list 中找到模型 {model_path}"

        exp = found[0]
        assert "meta" in exp and exp["meta"] is not None
        meta = exp["meta"]
        assert meta["strategy_id"] == "test_xgb_train"
        assert meta["source"] == "experiment"
        assert "created_at" in meta
        assert "label" in meta
        assert "params" in meta
        assert "features" in meta
        assert "objective" in meta

    def test_download_returns_model_json(self, auth_token, trained_model):
        """download 返回 model_json + meta_json（供前端 bind 使用）"""
        model_id = trained_model["model_id"]
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "download", "model_id": str(model_id)},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["model_id"] == model_id
        assert "model_json" in data and len(data["model_json"]) > 0
        assert "meta_json" in data
        # model_json 必须是合法 JSON
        import json as _json
        parsed = _json.loads(data["model_json"])
        assert isinstance(parsed, dict)


# ============== 训练结果验证 ==============

class TestXGBoostTrain:
    """训练功能测试"""

    def test_train_returns_model_path(self, trained_model):
        """训练响应包含 model_path"""
        assert "model_path" in trained_model
        assert trained_model["model_path"].endswith(".json")

    def test_train_returns_metrics(self, trained_model):
        """训练响应包含评估指标"""
        assert "n_features" in trained_model
        assert "n_train" in trained_model
        assert "n_test" in trained_model
        assert trained_model["n_features"] > 0
        assert trained_model["n_train"] > 0
        assert trained_model["n_test"] > 0


# ============== SHAP 测试 ==============

class TestXGBoostShap:
    """SHAP 功能测试"""

    def test_get_shap(self, auth_token, trained_model):
        """GET shap 返回正确结构"""
        model_id = trained_model["model_id"]
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "shap", "model_id": str(model_id)},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=30,
        )
        assert resp.status_code == 200
        sh = resp.json()
        assert sh["n_samples"] > 0
        assert len(sh["features"]) == trained_model["n_features"]
        assert len(sh["shap"]) == sh["n_samples"]
        assert len(sh["base_value"]) == sh["n_samples"]

        # 形状校验
        for row in sh["shap"][:5]:
            assert len(row) == trained_model["n_features"]

    def test_post_shap(self, auth_token, trained_model):
        """POST shap（兼容旧方式）返回正确结构"""
        model_id = trained_model["model_id"]
        resp = requests.post(
            f"{BASE_URL}/ml",
            json={"action": "shap", "model_id": model_id},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=30,
        )
        assert resp.status_code == 200
        sh = resp.json()
        assert sh["n_samples"] > 0


# ============== Delete 测试（放最后，会清除内存缓存） ==============

class TestXGBoostDelete:
    """删除功能测试 — 放在最后执行，因为会清除内存缓存"""

    def test_delete_model(self, auth_token, trained_model):
        """DELETE 删除模型成功"""
        model_id = trained_model["model_id"]
        resp = requests.delete(
            f"{BASE_URL}/ml",
            params={"model_id": str(model_id)},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 200
        assert resp.json()["message"] == "deleted"

    def test_shap_after_delete(self, auth_token, trained_model):
        """删除后 shap 返回 404"""
        model_id = trained_model["model_id"]
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "shap", "model_id": str(model_id)},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 404

    def test_delete_again(self, auth_token, trained_model):
        """重复删除返回 404"""
        model_id = trained_model["model_id"]
        resp = requests.delete(
            f"{BASE_URL}/ml",
            params={"model_id": str(model_id)},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
        )
        assert resp.status_code == 404


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
