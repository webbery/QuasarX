"""
策略关联模型清单 API 测试

覆盖：
- POST /v0/strategy {action:"batch_models", names:[...]}  批量返回策略关联的 XGBoost 模型信息
- POST /v0/ml        {action:"update_meta", model_path, fields} 修改 .meta.json 白名单字段

设计原则：
- 测试前在磁盘上准备 .json + .meta.json 文件，模拟训练 + 发布的结果
- 测试后清理文件，避免污染磁盘
- 测试 helper 直接拷贝已有测试中的 _headers 模式，不抽取公共函数
"""

import json
import time
import shutil
from pathlib import Path
import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False


# 拷贝 test_xgboost.py 的 _headers 写法（按"测试 helper 复制而非抽取"偏好）
def _headers(auth_token):
    return {"Authorization": auth_token}


# Service 数据库路径（与 config.json 中的 database_path 对齐）
# pytest 在 service/test/ 目录运行，服务在 service/build/ 目录运行，
# 因此相对路径需要走 build/data/
SERVICE_BUILD_DIR = Path(__file__).parent.parent.parent / "build"
SCRIPTS_DIR = SERVICE_BUILD_DIR / "scripts"
MODELS_DIR = SERVICE_BUILD_DIR / "data" / "models"
EXP_DIR = MODELS_DIR / "experiments"
PROD_DIR = MODELS_DIR / "production"


# ============== batch_models 测试 ==============

class TestBatchModels:
    """POST /v0/strategy {action:'batch_models', names:[...]} 测试"""

    def _write_test_strategy(self, name: str, xgb_node_id: str, xgb_label: str,
                             model_file: str) -> Path:
        """在 scripts/{name} 写一个含 XGBoostNode 的最小策略 JSON。返回写入文件路径。"""
        SCRIPTS_DIR.mkdir(parents=True, exist_ok=True)
        strategy = {
            "id": name,
            "graph": {
                "nodes": [
                    {
                        "id": xgb_node_id,
                        "type": "custom",
                        "data": {
                            "label": xgb_label,
                            "nodeType": "xgboost",
                            "params": {
                                "modelFile": {"value": model_file, "type": "text"},
                            },
                        },
                        "position": {"x": 0, "y": 0},
                    },
                ],
                "edges": [],
            },
        }
        script_path = SCRIPTS_DIR / name
        script_path.write_text(json.dumps(strategy))
        return script_path

    def _write_test_model(self, prod_dir: Path, exp_dir: Path,
                          strategy_name: str, xgb_label: str,
                          prod_created_at: str | None,
                          exp_created_at: str | None,
                          exp_filename_ts: str = "20260101_000000"):
        """准备 production + experiments 模型 + meta 文件，返回 (prod_meta_path, exp_meta_path)"""
        prod_dir.mkdir(parents=True, exist_ok=True)
        exp_dir.mkdir(parents=True, exist_ok=True)

        # production 模型 + meta（可能不存在）
        prod_model_name = f"{strategy_name}-{xgb_label}.json"
        prod_meta_name = f"{strategy_name}-{xgb_label}.meta.json"
        prod_model_path = prod_dir / prod_model_name
        prod_meta_path = prod_dir / prod_meta_name
        if prod_created_at:
            prod_model_path.write_text("{}")  # 占位
            prod_meta_path.write_text(json.dumps({
                "strategy_id": strategy_name,
                "created_at": prod_created_at,
                "model_type": "xgboost",
                "n_features": 5,
                "features": ["f1", "f2", "f3", "f4", "f5"],
            }))

        # experiment 模型 + meta（可能不存在）
        exp_name = f"{strategy_name}_{exp_filename_ts}"
        exp_model_path = exp_dir / f"{exp_name}.json"
        exp_meta_path = exp_dir / f"{exp_name}.meta.json"
        if exp_created_at:
            exp_model_path.write_text("{}")
            exp_meta_path.write_text(json.dumps({
                "strategy_id": strategy_name,
                "created_at": exp_created_at,
                "model_type": "xgboost",
                "n_features": 6,
                "features": ["f1", "f2", "f3", "f4", "f5", "f6"],
            }))

        return prod_meta_path, exp_meta_path

    def test_missing_names_returns_400(self, auth_token):
        """缺 names 字段返回 400"""
        resp = requests.post(
            f"{BASE_URL}/strategy",
            json={"action": "batch_models"},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=10,
        )
        assert resp.status_code == 400, f"expected 400 got {resp.status_code}: {resp.text}"

    def test_names_empty_array_returns_empty(self, auth_token):
        """空 names 数组返回空 map"""
        resp = requests.post(
            f"{BASE_URL}/strategy",
            json={"action": "batch_models", "names": []},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=10,
        )
        assert resp.status_code == 200
        assert resp.json() == {}

    def test_nonexistent_strategy_returns_error_field(self, auth_token):
        """不存在的策略返回 error 字段（不是 500）"""
        resp = requests.post(
            f"{BASE_URL}/strategy",
            json={"action": "batch_models", "names": ["nonexistent_strategy_for_test"]},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=10,
        )
        assert resp.status_code == 200
        body = resp.json()
        assert "nonexistent_strategy_for_test" in body
        item = body["nonexistent_strategy_for_test"]
        assert "models" in item
        assert item["models"] == []
        assert "error" in item  # 策略文件缺失 → 错误说明

    def test_strategy_with_xgboost_no_production(self, auth_token):
        """策略含 XGBoostNode 但 production 模型不存在 → production=null, is_latest=false"""
        name = "test_batch_models_no_prod"
        xgb_label = "xgb"
        self._write_test_strategy(name, "node1", xgb_label, f"production/{name}-{xgb_label}.json")
        # 不创建 production / experiments 文件
        try:
            resp = requests.post(
                f"{BASE_URL}/strategy",
                json={"action": "batch_models", "names": [name]},
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
            assert resp.status_code == 200
            item = resp.json().get(name, {})
            assert len(item["models"]) == 1
            m = item["models"][0]
            assert m["label"] == xgb_label
            assert m["production"] is None
            assert m["latest_experiment"] is None
            assert m["is_latest"] is False
        finally:
            (SCRIPTS_DIR / name).unlink(missing_ok=True)

    def test_strategy_with_production_newer_exp(self, auth_token):
        """production 早于 latest experiment → is_latest=false"""
        name = "test_batch_models_stale"
        xgb_label = "xgb"
        self._write_test_strategy(name, "node1", xgb_label, f"production/{name}-{xgb_label}.json")
        self._write_test_model(
            PROD_DIR, EXP_DIR, name, xgb_label,
            prod_created_at="2026-01-01T10:00:00",
            exp_created_at="2026-02-01T10:00:00",
            exp_filename_ts="20260201_100000",
        )
        try:
            resp = requests.post(
                f"{BASE_URL}/strategy",
                json={"action": "batch_models", "names": [name]},
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
            assert resp.status_code == 200
            item = resp.json().get(name, {})
            assert len(item["models"]) == 1
            m = item["models"][0]
            assert m["production"] is not None
            assert m["latest_experiment"] is not None
            assert m["is_latest"] is False, "production 早于实验时 is_latest 应为 false"
        finally:
            self._cleanup(name, xgb_label, exp_filename_ts="20260201_100000")

    def test_strategy_with_production_latest(self, auth_token):
        """production 与最新实验同时刻 → is_latest=true（>=）"""
        name = "test_batch_models_fresh"
        xgb_label = "xgb"
        same_ts = "2026-03-15T12:00:00"
        self._write_test_strategy(name, "node1", xgb_label, f"production/{name}-{xgb_label}.json")
        self._write_test_model(
            PROD_DIR, EXP_DIR, name, xgb_label,
            prod_created_at=same_ts,
            exp_created_at=same_ts,
            exp_filename_ts="20260315_120000",
        )
        try:
            resp = requests.post(
                f"{BASE_URL}/strategy",
                json={"action": "batch_models", "names": [name]},
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
            assert resp.status_code == 200
            m = resp.json()[name]["models"][0]
            assert m["is_latest"] is True, "production.created_at == exp.created_at 应判为最新"
        finally:
            self._cleanup(name, xgb_label, exp_filename_ts="20260315_120000")

    def test_batch_multiple_strategies(self, auth_token):
        """批量请求多个策略（混合：有效 + 磁盘上完全不存在）"""
        name_ok = "test_batch_models_multi_ok"
        name_missing = "test_batch_models_multi_missing"
        # 只为 name_ok 写策略文件 + 模型，name_missing 完全不创建
        self._write_test_strategy(name_ok, "node1", "xgb", f"production/{name_ok}-xgb.json")
        self._write_test_model(
            PROD_DIR, EXP_DIR, name_ok, "xgb",
            prod_created_at="2026-04-01T08:00:00",
            exp_created_at=None,
        )
        try:
            resp = requests.post(
                f"{BASE_URL}/strategy",
                json={"action": "batch_models", "names": [name_ok, name_missing]},
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
            assert resp.status_code == 200
            body = resp.json()
            assert "models" in body[name_ok]
            assert len(body[name_ok]["models"]) == 1
            assert body[name_ok]["models"][0]["is_latest"] is True  # 无实验 = 最新
            # 完全不存在的策略应返回 error 字段（不是 500）
            assert "error" in body[name_missing]
            assert body[name_missing]["models"] == []
        finally:
            (SCRIPTS_DIR / name_ok).unlink(missing_ok=True)
            (PROD_DIR / f"{name_ok}-xgb.json").unlink(missing_ok=True)
            (PROD_DIR / f"{name_ok}-xgb.meta.json").unlink(missing_ok=True)

    def _cleanup(self, strategy_name, xgb_label, exp_filename_ts):
        """清理测试写入的所有文件"""
        (SCRIPTS_DIR / strategy_name).unlink(missing_ok=True)
        (PROD_DIR / f"{strategy_name}-{xgb_label}.json").unlink(missing_ok=True)
        (PROD_DIR / f"{strategy_name}-{xgb_label}.meta.json").unlink(missing_ok=True)
        (EXP_DIR / f"{strategy_name}_{exp_filename_ts}.json").unlink(missing_ok=True)
        (EXP_DIR / f"{strategy_name}_{exp_filename_ts}.meta.json").unlink(missing_ok=True)


# ============== update_meta 测试 ==============

class TestUpdateMeta:
    """POST /v0/ml {action:'update_meta', ...} 测试"""

    def _write_dummy_meta(self, prod_dir: Path, strategy_name: str, xgb_label: str,
                          extra_meta: dict | None = None) -> Path:
        """写入 production meta，返回 meta 文件绝对路径。"""
        prod_dir.mkdir(parents=True, exist_ok=True)
        meta = {
            "strategy_id": strategy_name,
            "created_at": "2026-05-01T10:00:00",
            "model_type": "xgboost",
            "n_features": 5,
        }
        if extra_meta:
            meta.update(extra_meta)
        meta_path = prod_dir / f"{strategy_name}-{xgb_label}.meta.json"
        meta_path.write_text(json.dumps(meta))
        # 同时写一个空的 .json 让 update_meta 能找到路径
        (prod_dir / f"{strategy_name}-{xgb_label}.json").write_text("{}")
        return meta_path

    def test_missing_model_path_returns_400(self, auth_token):
        """缺 model_path 返回 400"""
        resp = requests.post(
            f"{BASE_URL}/ml",
            json={"action": "update_meta", "fields": {"version": "v1"}},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=10,
        )
        assert resp.status_code == 400

    def test_path_outside_model_dirs_returns_403(self, auth_token, tmp_path):
        """路径不在 experiments/ 或 production/ 下时返回 403"""
        bad_path = tmp_path / "evil.json"
        bad_path.write_text("{}")
        resp = requests.post(
            f"{BASE_URL}/ml",
            json={
                "action": "update_meta",
                "model_path": str(bad_path),
                "fields": {"version": "v1"},
            },
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=10,
        )
        assert resp.status_code == 403
        assert "path not in model directories" in resp.text

    def test_missing_fields_returns_400(self, auth_token):
        """缺 fields 字段返回 400"""
        # 用一个合法的 production 路径（即便不存在也应先做字段校验）
        logical_path = "production/nonexistent_for_test-xgb.json"
        resp = requests.post(
            f"{BASE_URL}/ml",
            json={
                "action": "update_meta",
                "model_path": logical_path,
                # 无 fields
            },
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=10,
        )
        assert resp.status_code == 400
        assert "fields" in resp.text

    def test_non_whitelisted_fields_ignored(self, auth_token):
        """非白名单字段被忽略，返回 400（updated=0）"""
        strategy_name = "test_update_meta_non_whitelist"
        xgb_label = "xgb"
        meta_path = self._write_dummy_meta(PROD_DIR, strategy_name, xgb_label)
        try:
            resp = requests.post(
                f"{BASE_URL}/ml",
                json={
                    "action": "update_meta",
                    "model_path": f"production/{strategy_name}-{xgb_label}.json",
                    "fields": {"strategy_id": "hacked", "n_features": 999},
                },
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
            assert resp.status_code == 400
            assert "no whitelisted fields" in resp.text
            # 验证文件未被修改
            current = json.loads(meta_path.read_text())
            assert current["strategy_id"] == strategy_name
            assert current["n_features"] == 5
        finally:
            (PROD_DIR / f"{strategy_name}-{xgb_label}.json").unlink(missing_ok=True)
            meta_path.unlink(missing_ok=True)

    def test_update_version_and_display_name(self, auth_token):
        """正常更新 version + display_name + description，写入 updated_at"""
        strategy_name = "test_update_meta_happy"
        xgb_label = "xgb"
        meta_path = self._write_dummy_meta(PROD_DIR, strategy_name, xgb_label)
        try:
            resp = requests.post(
                f"{BASE_URL}/ml",
                json={
                    "action": "update_meta",
                    "model_path": f"production/{strategy_name}-{xgb_label}.json",
                    "fields": {
                        "version": "v17",
                        "display_name": "CTA_v16_baseline",
                        "description": "15 features, vol regime normalized",
                    },
                },
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
            assert resp.status_code == 200, resp.text
            body = resp.json()
            assert body["status"] == "ok"
            assert body["meta"]["version"] == "v17"
            assert body["meta"]["display_name"] == "CTA_v16_baseline"
            assert body["meta"]["description"] == "15 features, vol regime normalized"
            assert "updated_at" in body["meta"]
            # 验证文件落盘
            saved = json.loads(meta_path.read_text())
            assert saved["version"] == "v17"
            # 原字段保留
            assert saved["strategy_id"] == strategy_name
            assert saved["n_features"] == 5
        finally:
            (PROD_DIR / f"{strategy_name}-{xgb_label}.json").unlink(missing_ok=True)
            meta_path.unlink(missing_ok=True)

    def test_update_meta_via_logical_path_with_subdir(self, auth_token):
        """逻辑路径含子目录时仍能正确解析"""
        strategy_name = "test_update_meta_subdir"
        xgb_label = "xgb"
        meta_path = self._write_dummy_meta(PROD_DIR, strategy_name, xgb_label)
        try:
            resp = requests.post(
                f"{BASE_URL}/ml",
                json={
                    "action": "update_meta",
                    "model_path": f"production/{strategy_name}-{xgb_label}.json",
                    "fields": {"version": "v2", "display_name": "subdir_baseline"},
                },
                headers=_headers(auth_token),
                verify=VERIFY_SSL,
                timeout=10,
            )
            assert resp.status_code == 200, resp.text
            assert resp.json()["meta"]["version"] == "v2"
            assert resp.json()["meta"]["display_name"] == "subdir_baseline"
        finally:
            (PROD_DIR / f"{strategy_name}-{xgb_label}.json").unlink(missing_ok=True)
            meta_path.unlink(missing_ok=True)