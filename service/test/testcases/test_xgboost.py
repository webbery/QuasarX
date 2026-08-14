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
import tempfile
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


# ============== 端到端：训练 → 下载 → 部署 → 回测推理 ==============

E2E_DEPLOY_NAME = "test_xgb_e2e"
E2E_XGB_LABEL = "XGBoost"
E2E_SYMBOL = "sz.800001"
# DebugNode CSV 中的特征列名（FunctionNode 输出 key = symbol.label）
E2E_FEATURE_COLS = [
    f"{E2E_SYMBOL}.MA(5)",
    f"{E2E_SYMBOL}.STD(5)",
    f"{E2E_SYMBOL}.Return(1)",
]
E2E_PROB_COLS = [
    f"{E2E_SYMBOL}.xgb_probs_0",
    f"{E2E_SYMBOL}.xgb_probs_1",
    f"{E2E_SYMBOL}.xgb_probs_2",
]
E2E_PRED_COL = f"{E2E_SYMBOL}.xgb_prediction"

# DebugNode CSV 路径（服务 build 目录）
_SERVICE_ROOT = Path(__file__).parent.parent.parent
_E2E_DEBUG_DIR = _SERVICE_ROOT / "build" / "data" / "data" / "debug"


def _build_e2e_strategy(name=E2E_DEPLOY_NAME, xgb_label=E2E_XGB_LABEL):
    """构建端到端回测策略：Input → MA/STD/Return → XGBoost + DebugNode"""
    model_file = f"production/{name}-{xgb_label}.json"
    return {
        "id": name, "name": name, "version": 1, "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "行情数据", "nodeType": "input",
                      "params": {"code": {"value": [E2E_SYMBOL], "type": "text"},
                                 "freq": {"value": "1d", "type": "select"},
                                 "close": {"value": "close", "type": "text"},
                                 "open": {"value": "open", "type": "text"},
                                 "high": {"value": "high", "type": "text"},
                                 "low": {"value": "low", "type": "text"},
                                 "volume": {"value": "volume", "type": "text"}}}},
            {"id": "2", "type": "custom", "position": {"x": 200, "y": -80},
             "data": {"label": "MA(5)", "nodeType": "function",
                      "params": {"method": {"value": "MA", "type": "select"},
                                 "range": {"value": "5d", "type": "text"}}}},
            {"id": "3", "type": "custom", "position": {"x": 200, "y": 0},
             "data": {"label": "STD(5)", "nodeType": "function",
                      "params": {"method": {"value": "STD", "type": "select"},
                                 "range": {"value": "5d", "type": "text"}}}},
            {"id": "4", "type": "custom", "position": {"x": 200, "y": 80},
             "data": {"label": "Return(1)", "nodeType": "function",
                      "params": {"method": {"value": "Return", "type": "select"},
                                 "range": {"value": "1d", "type": "text"}}}},
            {"id": "5", "type": "custom", "position": {"x": 400, "y": 0},
             "data": {"label": xgb_label, "nodeType": "xgboost",
                      "params": {"modelFile": {"value": model_file, "type": "text"},
                                 "features": {"value": "", "type": "text"},
                                 "objective": {"value": "multi:softprob", "type": "select"},
                                 "num_class": {"value": 3, "type": "number"}}}},
            # DebugNode：捕获特征 + XGBoost 输出，供 Python 对比验证
            {"id": "6", "type": "custom", "position": {"x": 600, "y": 0},
             "data": {"label": "debug_xgb", "nodeType": "debug",
                      "params": {"suffix": {"value": "csv", "type": "select"}}}},
        ],
        "edges": [
            {"id": "e1->2", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "e1->3", "source": "1", "target": "3",
             "sourceHandle": "1-close", "targetHandle": "3", "type": "default"},
            {"id": "e1->4", "source": "1", "target": "4",
             "sourceHandle": "1-close", "targetHandle": "4", "type": "default"},
            {"id": "e2->5", "source": "2", "target": "5",
             "sourceHandle": "2", "targetHandle": "5", "type": "default"},
            {"id": "e3->5", "source": "3", "target": "5",
             "sourceHandle": "3", "targetHandle": "5", "type": "default"},
            {"id": "e4->5", "source": "4", "target": "5",
             "sourceHandle": "4", "targetHandle": "5", "type": "default"},
            # DebugNode 连接所有上游输出
            {"id": "e2->6", "source": "2", "target": "6",
             "sourceHandle": "2", "targetHandle": "6", "type": "default"},
            {"id": "e3->6", "source": "3", "target": "6",
             "sourceHandle": "3", "targetHandle": "6", "type": "default"},
            {"id": "e4->6", "source": "4", "target": "6",
             "sourceHandle": "4", "targetHandle": "6", "type": "default"},
            {"id": "e5->6", "source": "5", "target": "6",
             "sourceHandle": "5", "targetHandle": "6", "type": "default"},
        ],
    }


def _cleanup_e2e_strategy(auth_token):
    """停止并删除 E2E 测试策略"""
    try:
        requests.post(f"{BASE_URL}/strategy",
                      json={"mode": 2, "name": E2E_DEPLOY_NAME},
                      headers=_headers(auth_token), verify=VERIFY_SSL, timeout=5)
        requests.delete(f"{BASE_URL}/strategy",
                        json={"name": E2E_DEPLOY_NAME},
                        headers=_headers(auth_token), verify=VERIFY_SSL, timeout=5)
    except Exception:
        pass


def _read_e2e_debug_csv() -> "pd.DataFrame":
    """读取 E2E 测试 DebugNode 输出的 CSV"""
    import pandas as pd
    csv_path = _E2E_DEBUG_DIR / E2E_DEPLOY_NAME / "debug_xgb.csv"
    assert csv_path.exists(), f"Debug CSV not found: {csv_path}"
    return pd.read_csv(csv_path)


def _xgb_predict_python(model_path: str, feature_df: "pd.DataFrame"):
    """用 Python xgboost 加载同一模型推理，返回概率矩阵"""
    import xgboost as xgb
    bst = xgb.Booster()
    bst.load_model(model_path)
    dm = xgb.DMatrix(feature_df)
    return bst.predict(dm)


class TestXGBoostE2E:
    """端到端：训练 → 下载模型 → 部署策略(绑定模型) → 回测推理

    三层验证：
      Level 1 — 回测指标（features）为有限数值
      Level 2 — XGBoost 输出非全零、概率和 ≈ 1
      Level 3 — Python xgboost 加载同模型推理，与 C++ 逐值对比

    必须在 TestXGBoostDelete 之前执行（依赖内存中的模型缓存）。
    """

    @pytest.fixture(scope="class")
    def _deployed(self, auth_token, trained_model):
        """download + deploy + 保存模型临时文件（class 共享）"""
        # 下载训练产物
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "download", "model_id": str(trained_model["model_id"])},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=30,
        )
        assert resp.status_code == 200, f"download 失败: {resp.text}"
        dl = resp.json()
        assert len(dl["model_json"]) > 0

        # 保存模型到临时文件（Python xgboost 加载用）
        model_tmp = tempfile.NamedTemporaryFile(
            suffix=".json", delete=False, mode="w")
        model_tmp.write(dl["model_json"])
        model_tmp.close()

        # 构建策略 + multipart deploy
        strategy = _build_e2e_strategy()
        files = {
            "script": ("script.json", json.dumps(strategy).encode(), "application/json"),
            f"model_{E2E_XGB_LABEL}": (
                f"{E2E_XGB_LABEL}.json", dl["model_json"].encode(), "application/json"),
        }
        if dl.get("meta_json"):
            files[f"model_{E2E_XGB_LABEL}_meta"] = (
                f"{E2E_XGB_LABEL}.meta.json",
                dl["meta_json"].encode(), "application/json")

        resp = requests.post(
            f"{BASE_URL}/strategy", files=files,
            data={"name": E2E_DEPLOY_NAME},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=60,
        )
        assert resp.status_code == 200, f"deploy 失败: {resp.status_code} {resp.text}"

        yield {"strategy": strategy, "download": dl, "model_path": model_tmp.name}

        # 清理
        _cleanup_e2e_strategy(auth_token)
        try:
            Path(model_tmp.name).unlink(missing_ok=True)
        except Exception:
            pass

    # ---------- Level 1: 回测不崩溃 + DebugNode 有输出 ----------

    def test_backtest_with_deployed_model(self, auth_token, _deployed):
        """回测加载 production 模型推理不崩溃，DebugNode 产出 CSV"""
        resp = requests.post(
            f"{BASE_URL}/backtest",
            json={"script": json.dumps(_deployed["strategy"]), "validate": False},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=120,
        )
        assert resp.status_code == 200, f"回测失败: {resp.text}"

        # 无 Signal/Execution 时响应可能为 null，这是正常的
        # 核心验证：DebugNode CSV 存在且有数据（说明 XGBoostNode 推理执行了）
        df = _read_e2e_debug_csv()
        assert len(df) > 0, "Debug CSV 为空，回测可能未执行"

        # 概率列存在且有非 NaN 行
        for col in E2E_PROB_COLS:
            assert col in df.columns, f"CSV 缺少列 {col}，实际列: {list(df.columns)}"
        probs = df[E2E_PROB_COLS].astype(float)
        valid_rows = probs.notna().any(axis=1)
        assert valid_rows.sum() > 0, "概率列全为 NaN，XGBoost 可能未推理"

    # ---------- Level 2: XGBoost 输出 ----------

    def test_xgboost_output_valid(self, auth_token, _deployed):
        """DebugNode CSV 中 XGBoost 输出：非全零、概率和 ≈ 1"""
        import numpy as np

        # 先跑回测（触发 DebugNode 写 CSV）
        resp = requests.post(
            f"{BASE_URL}/backtest",
            json={"script": json.dumps(_deployed["strategy"]), "validate": False},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=120,
        )
        assert resp.status_code == 200, f"回测失败: {resp.text}"

        df = _read_e2e_debug_csv()

        # 概率列存在
        for col in E2E_PROB_COLS + [E2E_PRED_COL]:
            assert col in df.columns, f"CSV 缺少列 {col}，实际列: {list(df.columns)}"

        # 取有效行（非 NaN）
        probs = df[E2E_PROB_COLS].astype(float)
        valid_mask = probs.notna().all(axis=1)
        valid_probs = probs[valid_mask]
        assert len(valid_probs) > 0, "所有概率行都是 NaN，XGBoost 可能未推理"

        # 非全零
        assert (valid_probs.values != 0).any(), "概率全为 0，模型可能未正确加载"

        # 每行概率和 ≈ 1（multi:softprob 输出）
        row_sums = valid_probs.sum(axis=1)
        np.testing.assert_allclose(row_sums.values, 1.0, atol=1e-5,
                                   err_msg="概率行和不等于 1")

    # ---------- Level 3: Python 黄金标准对比 ----------

    def test_cpp_vs_python_prediction(self, auth_token, _deployed):
        """C++ XGBoostNode 推理结果与 Python xgboost 逐值一致"""
        import numpy as np
        import pandas as pd

        # 先跑回测
        resp = requests.post(
            f"{BASE_URL}/backtest",
            json={"script": json.dumps(_deployed["strategy"]), "validate": False},
            headers=_headers(auth_token),
            verify=VERIFY_SSL,
            timeout=120,
        )
        assert resp.status_code == 200, f"回测失败: {resp.text}"

        df = _read_e2e_debug_csv()

        # ---- 3a: 验证 MA(5) 特征计算正确（pandas 独立重算） ----
        csv_data_dir = _SERVICE_ROOT / "build" / "data" / "A_hfq"
        close_df = pd.read_csv(csv_data_dir / f"{E2E_SYMBOL}.csv")
        close_prices = close_df["close"].values.astype(float)
        py_ma5 = pd.Series(close_prices).rolling(5).mean().values  # 前4个 NaN

        cpp_ma5 = df[f"{E2E_SYMBOL}.MA(5)"].astype(float).values
        n = min(len(cpp_ma5), len(py_ma5))
        # 跳过前 4 个 NaN（窗口期）
        start = 4
        np.testing.assert_allclose(
            cpp_ma5[start:n], py_ma5[start:n], atol=1e-6,
            err_msg="C++ MA(5) 与 pandas rolling.mean 不一致")

        # ---- 3b: 验证 XGBoost 推理正确（Python xgboost 独立推理） ----
        import xgboost as xgb
        feat_df = df[E2E_FEATURE_COLS].astype(float)
        valid_mask = feat_df.notna().all(axis=1)
        valid_features = feat_df[valid_mask]
        assert len(valid_features) > 0, "无有效特征行"

        # 从模型读取实际特征顺序（模型知道自己训练时的特征名和顺序）
        bst = xgb.Booster()
        bst.load_model(_deployed["model_path"])
        prefix = E2E_SYMBOL + "."

        if bst.feature_names:
            # 模型有 feature_names（短名），将 CSV 全名映射到短名后按模型顺序重排
            full_to_short = {col: col[len(prefix):] for col in E2E_FEATURE_COLS}
            short_to_full = {v: k for k, v in full_to_short.items()}
            ordered_full = [short_to_full[name] for name in bst.feature_names]
            valid_features = valid_features[ordered_full]
            valid_features.columns = bst.feature_names
        else:
            # 模型无 feature_names，按 CSV 列顺序去前缀
            valid_features.columns = [col[len(prefix):] for col in E2E_FEATURE_COLS]

        # Python 推理（用 float32 与 C++ 精度一致，避免 float64→float32 转换差异）
        py_probs = _xgb_predict_python(_deployed["model_path"], valid_features.astype('float32'))

        # C++ 结果
        cpp_probs = df[E2E_PROB_COLS].astype(float)[valid_mask.values].values
        cpp_preds = df[E2E_PRED_COL].astype(float)[valid_mask.values].values

        # 概率对比（容差 1e-4）
        np.testing.assert_allclose(
            cpp_probs, py_probs, atol=1e-4,
            err_msg="C++ vs Python 概率不一致")

        # 预测类别对比
        py_preds = np.argmax(py_probs, axis=1)
        np.testing.assert_array_equal(
            cpp_preds, py_preds,
            err_msg="C++ vs Python 预测类别不一致")


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
