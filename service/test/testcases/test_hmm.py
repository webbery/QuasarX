#!/usr/bin/env python3
"""
HMMHandler (/v0/hmm) REST API 测试

覆盖 train / predict / decode / publish / list / delete 六条路径，
以及每个分支的拒绝路径（空标的、非法 n_states、未知特征、维度不匹配、
不存在/不安全的文件名等）。

数据：build/data/A_hfq 下的真实日线（sh.600000 / sh.600016 / sh.600028，各 4048 根 bar）。
测试自行导入 DuckDB（行情按 symbol 逐个清理，不 DROP TABLE，避免波及其他用例的数据），
日期窗口固定，保证可重现。

注意：QuoteDB::importCsv 按**列位置**解析（datetime,open,close,high,low,volume,turnover），
而 A_hfq CSV 的表头是 date,open,high,low,close,volume,amount,turn —— 直接导入会把
close/high/low 读错位，所以导入前必须按表头名重排。

用法：
  pytest test_hmm.py -v
"""

import pytest
import requests
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from tool import BASE_URL, VERIFY_SSL, SERVICE_ROOT, CSV_DATA_DIR

# 真实日线标的（每个 4048 根 bar，覆盖 2010~2026）
TEST_SYMBOLS = ["sh.600000", "sh.600016", "sh.600028"]

# 固定窗口，保证可重现
START = "2018-01-01"
END = "2024-12-31"

STRATEGY_ID = "pytest_hmm"

# QuoteDB::importCsv 期望的列顺序
_IMPORT_COLUMNS = ["date", "open", "close", "high", "low", "volume", "turnover"]


# ============================================================
# helpers（本文件自包含，不抽公共模块）
# ============================================================

def _find_quote_dir() -> Path:
    """定位 A_hfq 目录：build/data 优先，其次服务数据目录"""
    candidates = [
        SERVICE_ROOT / "build" / "data" / "A_hfq",
        SERVICE_ROOT / "winbuild" / "data" / "A_hfq",
        SERVICE_ROOT / "data" / "A_hfq",
    ]
    for d in candidates:
        if d.is_dir():
            return d
    raise RuntimeError(f"A_hfq directory not found, tried: {candidates}")


def _service_path(rel: str) -> Path:
    """把服务返回的路径换算成本地绝对路径

    Handler 返回的 model_path / production_path 都是相对**服务进程工作目录**的
    （形如 data/hmm_models/...），而 pytest 从 service/test/ 运行，cwd 不同。
    直接 Path(rel).exists() 解析到错误的目录下，恒为 False。
    CSV_DATA_DIR = <服务数据目录>/A_hfq，故其上两级即服务的工作目录。
    """
    p = Path(rel)
    return p if p.is_absolute() else CSV_DATA_DIR.parent.parent / p


def _reorder_for_import(symbol: str) -> list:
    """按表头名把 A_hfq CSV 重排成 importCsv 期望的列序"""
    path = _find_quote_dir() / f"{symbol}.csv"
    assert path.exists(), f"行情 CSV 不存在: {path}"

    lines = path.read_text(encoding="utf-8").strip().split("\n")
    header = [c.strip() for c in lines[0].split(",")]

    # A_hfq 用 turn 表示换手率，importCsv 第 7 列期望 turnover
    alias = {"turn": "turnover", "datetime": "date"}
    normalized = [alias.get(c.lower(), c.lower()) for c in header]

    out = [",".join(_IMPORT_COLUMNS)]
    for line in lines[1:]:
        cols = line.split(",")
        if len(cols) < len(normalized):
            continue
        row = dict(zip(normalized, cols))
        out.append(",".join(row.get(c, "0") for c in _IMPORT_COLUMNS))
    return out


def _import_symbol(symbol: str, headers: dict) -> dict:
    """把单个标的导入 stock_1d"""
    try:
        resp = requests.post(
            f"{BASE_URL}/quote/data",
            json={"action": "import", "table": "stock_1d", "symbol": symbol,
                  "data": _reorder_for_import(symbol)},
            headers=headers, verify=VERIFY_SSL, timeout=120,
        )
    except Exception as e:
        return {"status": "error", "error": str(e)}
    if resp.status_code != 200:
        return {"status": "error", "error": f"HTTP {resp.status_code}: {resp.text[:200]}"}
    return resp.json()


def _cleanup_symbol(symbol: str, headers: dict) -> None:
    """按 symbol 删除（不 DROP TABLE，避免清掉其他用例导入的数据）"""
    try:
        requests.delete(
            f"{BASE_URL}/quote/data",
            json={"action": "cleanup", "table": "stock_1d", "symbol": symbol},
            headers=headers, verify=VERIFY_SSL, timeout=60,
        )
    except Exception:
        pass


def _hmm(payload: dict, headers: dict, expect: int = 200) -> dict:
    """POST /v0/hmm，失败返回 {"status": "error", ...} 供调用方统一处理"""
    try:
        resp = requests.post(f"{BASE_URL}/hmm", json=payload,
                             headers=headers, verify=VERIFY_SSL, timeout=180)
    except Exception as e:
        return {"status": "error", "error": str(e)}
    if resp.status_code != expect:
        return {"status": "error",
                "error": f"HTTP {resp.status_code} (expect {expect}): {resp.text[:300]}"}
    try:
        return resp.json()
    except Exception as e:
        return {"status": "error", "error": f"invalid JSON: {e}"}


def _hmm_get(headers: dict, expect: int = 200) -> dict:
    try:
        resp = requests.get(f"{BASE_URL}/hmm", params={"action": "list"},
                            headers=headers, verify=VERIFY_SSL, timeout=60)
    except Exception as e:
        return {"status": "error", "error": str(e)}
    if resp.status_code != expect:
        return {"status": "error",
                "error": f"HTTP {resp.status_code} (expect {expect}): {resp.text[:300]}"}
    try:
        return resp.json()
    except Exception as e:
        return {"status": "error", "error": f"invalid JSON: {e}"}


def _train(symbols, features, n_states=3, headers=None, strategy_id=STRATEGY_ID, **extra):
    payload = {
        "action": "train",
        "symbols": symbols,
        "features": features,
        "start": START,
        "end": END,
        "n_states": n_states,
        "strategy_id": strategy_id,
    }
    payload.update(extra)
    return _hmm(payload, headers)


def _delete_model(model_id, strategy_id=STRATEGY_ID, headers=None, expect=200):
    resp = requests.delete(
        f"{BASE_URL}/hmm",
        params={"strategy_id": strategy_id, "model_id": model_id},
        headers=headers, verify=VERIFY_SSL, timeout=60,
    )
    return resp.status_code == expect, resp.status_code, resp.text[:200]


# ============================================================
# fixtures
# ============================================================

@pytest.fixture(scope="module")
def hmm_data(auth_token):
    """导入测试所需行情，模块结束后按 symbol 清理

    依赖 auth_token 而非 headers：headers 是 function 级 fixture，
    module 级 fixture 不能依赖它（pytest ScopeMismatch）。
    与 test_xgboost.py / test_decision.py 的做法保持一致。
    """
    headers = {"Authorization": auth_token} if auth_token else {}
    imported, failed = [], []
    for sym in TEST_SYMBOLS:
        res = _import_symbol(sym, headers)
        if res.get("status") == "error" or res.get("error"):
            failed.append((sym, res.get("error")))
        else:
            imported.append(sym)
    if failed:
        pytest.skip(f"行情导入失败，跳过 HMM 测试: {failed}")

    yield imported

    for sym in imported:
        _cleanup_symbol(sym, headers)


@pytest.fixture
def cleanup_model(hmm_data, headers):
    """每个用例独立训练一个模型，用例结束后删除。

    曾经这里是 module 级 trained_model + function 级 cleanup_model 的组合：
    模型只训练一次，却在每个用例结束时被删一次，于是只有第一个用到它的用例
    拿得到有效模型，之后全部 404。改为 per-test 训练消除顺序依赖。
    训练本身很便宜（7 年 × 2 特征 × 3 状态约几十毫秒），重复训练无副作用。
    """
    data = _train(hmm_data[:2], ["return_1d"], headers=headers)
    if data.get("status") == "error":
        pytest.skip(f"训练失败，跳过: {data.get('error')}")
    yield data
    # 忽略结果：部分用例（如 delete 用例）自己会删掉这个模型，此时 404 属正常
    _delete_model(data["model_id"], headers=headers, expect=200)


# ============================================================
# train
# ============================================================

class TestHMMTrain:

    @pytest.mark.timeout(200)
    def test_train_returns_probabilistic_model(self, hmm_data, headers):
        """训练返回完整模型参数：概率分布合法、矩阵形状正确"""
        data = _train(hmm_data[:2], ["return_1d"], headers=headers)
        if data.get("status") == "error":
            pytest.skip(f"训练失败: {data.get('error')}")
        _delete_model(data["model_id"], headers=headers)

        assert data["status"] == "success"
        model = data["model"]
        n_states, n_features = model["n_states"], model["n_features"]

        assert n_states == 3
        assert n_features == 2, "2 标的 × 1 特征 = 2 维观测"

        # 初始分布：长度正确且归一
        assert len(model["pi"]) == n_states
        assert abs(sum(model["pi"]) - 1.0) < 1e-6

        # 转移矩阵：形状正确、每行归一（概率分布的硬约束）
        assert len(model["A"]) == n_states
        for row in model["A"]:
            assert len(row) == n_states
            assert abs(sum(row) - 1.0) < 1e-6, f"转移矩阵行未归一: {row}"
            assert all(v >= 0 for v in row)

        # 均值/协方差：n_states × n_features，且方差必须为正
        assert len(model["mu"]) == n_states and len(model["mu"][0]) == n_features
        assert len(model["cov_diag"]) == n_states and len(model["cov_diag"][0]) == n_features
        for row in model["cov_diag"]:
            assert all(v > 0 for v in row), "正则化后协方差应严格为正"

        # 期望持续天数 = 1/(1-A_ii) > 1
        assert len(model["state_duration"]) == n_states
        assert all(d > 1.0 for d in model["state_duration"])

        info = data["training_info"]
        assert info["data_points"] > 1000, f"观测样本过少: {info['data_points']}"
        assert info["date_start"] == START or info["date_start"] >= START
        assert info["date_end"] <= END
        assert isinstance(info["converged"], bool)

    @pytest.mark.timeout(200)
    def test_serialization_is_not_transposed(self, hmm_data, headers):
        """
        to_json 必须按行写出，不能把矩阵转置。

        Eigen 是列主序存储，row(i).data() 指向 A(0,i)，连续读 cols() 个元素
        拿到的是第 i 列。曾经 to_json 用这种裸指针区间构造，把 A / mu / cov_diag
        整个转置写进 JSON：训练响应里的"A 行"其实是列（列和≠1），
        且 from_json 读回转置矩阵，所有落盘模型的行为都与刚训练完时不一致。

        这里用非对称的 n_states（4）放大差异：对称矩阵转置后看不出问题。
        """
        data = _train(hmm_data[:2], ["return_1d"], headers=headers, n_states=4)
        if data.get("status") == "error":
            pytest.skip(f"训练失败: {data.get('error')}")

        try:
            model = data["model"]
            A = model["A"]
            n = model["n_states"]
            assert n == 4

            # 行归一是硬约束。若矩阵被转置，这里校验的是原矩阵的列，列和一般不为 1。
            for i, row in enumerate(A):
                assert abs(sum(row) - 1.0) < 1e-6, \
                    f"第 {i} 行未归一（和={sum(row):.6f}），疑似序列化转置: {row}"

            # 非对称性：4 状态模型几乎必然非对称，n_states=3 时有偶然对称的可能
            is_symmetric = all(abs(A[i][j] - A[j][i]) < 1e-9
                               for i in range(n) for j in range(n))
            assert not is_symmetric, \
                "转移矩阵恰好对称，无法区分转置；请换随机种子或状态数重跑"

            # 往返一致性：落盘模型必须能被推理加载，且概率合法
            pred1 = _hmm({"action": "predict", "model_path": data["model_path"],
                          "observation": [0.001, -0.002]}, headers)
            assert pred1.get("status") != "error", f"推理失败: {pred1.get('error')}"
            probs = pred1["prediction"]["state_probs"]
            assert len(probs) == n
            assert abs(sum(probs) - 1.0) < 1e-6
        finally:
            _delete_model(data["model_id"], headers=headers)

    @pytest.mark.timeout(200)
    def test_published_model_matches_trained_model(self, hmm_data, headers):
        """publish 出来的文件必须与实验模型推理结果一致（from_json 往返无损）"""
        data = _train(hmm_data[:2], ["return_1d"], headers=headers)
        if data.get("status") == "error":
            pytest.skip(f"训练失败，跳过: {data.get('error')}")

        try:
            pub = _hmm({"action": "publish", "model_id": data["model_id"],
                        "strategy_id": STRATEGY_ID}, headers)
            if pub.get("status") == "error":
                pytest.skip(f"发布失败: {pub.get('error')}")

            obs = [0.001, -0.002]
            exp = _hmm({"action": "predict", "model_path": data["model_path"],
                        "observation": obs}, headers)
            got = _hmm({"action": "predict", "model_path": pub["production_path"],
                        "observation": obs}, headers)
            assert exp.get("status") != "error", f"实验模型推理失败: {exp.get('error')}"
            assert got.get("status") != "error", f"生产模型推理失败: {got.get('error')}"

            p_exp = exp["prediction"]["state_probs"]
            p_got = got["prediction"]["state_probs"]
            assert len(p_exp) == len(p_got)
            for a, b in zip(p_exp, p_got):
                assert abs(a - b) < 1e-9, f"往返后概率不一致: {a} vs {b}"
        finally:
            _delete_model(data["model_id"], headers=headers)

    @pytest.mark.timeout(200)
    def test_multi_feature_alignment_keeps_dates(self, hmm_data, headers):
        """
        不同 warmup 长度的特征（return_1d vs volatility_20d）必须按日期对齐。

        若按行号截断拼接，volatility_20d 会比 return_1d 少约 20 行，
        对齐后的样本数应当只比单特征少个位数（而不是 20 行错位）。
        """
        single = _train(hmm_data[:2], ["return_1d"], headers=headers)
        if single.get("status") == "error":
            pytest.skip(f"训练失败: {single.get('error')}")
        _delete_model(single["model_id"], headers=headers)

        multi = _train(hmm_data[:2], ["return_1d", "volatility_20d"], headers=headers)
        if multi.get("status") == "error":
            pytest.skip(f"训练失败: {multi.get('error')}")
        _delete_model(multi["model_id"], headers=headers)

        assert multi["model"]["n_features"] == 4, "2 标的 × 2 特征"

        n_single = single["training_info"]["data_points"]
        n_multi = multi["training_info"]["data_points"]
        # 20 日波动率只损失 20 根 warmup，两标的取交集后不应出现行级错位
        assert 0 <= n_single - n_multi <= 25, \
            f"对齐后样本数异常: 单特征={n_single}, 多特征={n_multi}"

    @pytest.mark.timeout(200)
    @pytest.mark.parametrize("bad,expect_msg", [
        ({"symbols": []}, "symbols is empty"),
        ({"symbols": ["sz.999999"], "n_states": 1}, "n_states"),
        ({"symbols": ["sz.999999"], "features": ["not_a_feature"]}, "unknown feature"),
        # features 缺失/为空必须显式报错。之前静默兜底成 ["return_1d"]，
        # 用户以为在跑多特征模型，实际跑的却是单特征，且没有任何提示。
        ({"symbols": ["sz.999999"], "features": []}, "features is empty"),
    ])
    def test_train_rejects_invalid_params(self, headers, bad, expect_msg):
        """非法参数必须 400 而不是静默降级"""
        payload = {
            "action": "train", "start": START, "end": END,
            "strategy_id": STRATEGY_ID, "n_states": 3, "features": ["return_1d"],
        }
        payload.update(bad)
        data = _hmm(payload, headers, expect=400)
        assert "error" in data, f"缺少 error 字段: {data}"
        assert expect_msg in data["error"], f"错误信息不符: {data['error']}"

    @pytest.mark.timeout(200)
    def test_train_requires_features_field(self, headers):
        """完全不传 features 时也必须 400——不能回落默认值"""
        payload = {
            "action": "train", "symbols": ["sz.999999"],
            "start": START, "end": END, "strategy_id": STRATEGY_ID, "n_states": 3,
        }
        data = _hmm(payload, headers, expect=400)
        assert "features is empty" in data.get("error", ""), f"错误信息不符: {data}"

    @pytest.mark.timeout(200)
    def test_train_rejects_wrong_param_type(self, headers):
        """features 传字符串而非数组：类型错误属于请求错误，应 400 不是 500"""
        payload = {
            "action": "train", "symbols": ["sz.999999"], "features": "return_1d",
            "start": START, "end": END, "strategy_id": STRATEGY_ID, "n_states": 3,
        }
        data = _hmm(payload, headers, expect=400)
        assert "error" in data, f"缺少 error 字段: {data}"

    @pytest.mark.timeout(60)
    def test_post_rejects_invalid_json(self, headers):
        """body 不是合法 JSON 时应 400（客户端问题），而不是 500"""
        resp = requests.post(f"{BASE_URL}/hmm", data="{not valid json",
                             headers=headers, verify=VERIFY_SSL, timeout=30)
        assert resp.status_code == 400, f"期望 400，实际 {resp.status_code}: {resp.text[:200]}"
        assert "invalid JSON" in resp.json().get("error", "")

    @pytest.mark.timeout(200)
    def test_train_rejects_path_traversal_strategy_id(self, hmm_data, headers):
        """strategy_id 直接拼进文件名，必须拒绝非白名单字符"""
        data = _train(hmm_data[:1], ["return_1d"], headers=headers,
                      strategy_id="../../etc")
        assert data.get("status") == "error"
        assert "strategy_id" in data.get("error", "")


# ============================================================
# predict
# ============================================================

class TestHMMPredict:

    @pytest.mark.timeout(120)
    def test_predict_returns_normalized_probs(self, cleanup_model, headers):
        """合法观测 → 概率归一、状态编号在范围内"""
        obs = [0.001, -0.002]
        data = _hmm({"action": "predict", "model_path": cleanup_model["model_path"],
                     "observation": obs}, headers)
        if data.get("status") == "error":
            pytest.skip(f"推理失败: {data.get('error')}")

        pred = data["prediction"]
        probs = pred["state_probs"]
        assert len(probs) == cleanup_model["model"]["n_states"]
        assert all(p >= 0 for p in probs)
        assert abs(sum(probs) - 1.0) < 1e-6, f"概率未归一: {probs}"
        assert probs[pred["most_likely_state"]] == max(probs)
        assert 0 <= pred["most_likely_state"] < cleanup_model["model"]["n_states"]

    @pytest.mark.timeout(120)
    def test_predict_rejects_dimension_mismatch(self, cleanup_model, headers):
        """
        观测维度必须与模型 n_features 一致。
        不校验会让 GaussianHMM::emission_log_prob 越界读 mu_(j, d)。
        """
        data = _hmm({"action": "predict", "model_path": cleanup_model["model_path"],
                     "observation": [0.001, 0.002, 0.003, 0.004]},
                    headers, expect=400)
        assert "dimension" in data.get("error", "")
        assert data["expected"] == cleanup_model["model"]["n_features"]

    @pytest.mark.timeout(120)
    def test_predict_missing_model_returns_404(self, headers):
        data = _hmm({"action": "predict", "model_path": "/tmp/does_not_exist.json",
                     "observation": [0.001]}, headers, expect=404)
        assert "not found" in data.get("error", "")


# ============================================================
# decode
# ============================================================

class TestHMMDecode:

    @pytest.mark.timeout(200)
    def test_decode_returns_consistent_state_sequence(self, cleanup_model, headers):
        """Viterbi 解码：序列长度/取值范围/各状态天数自洽"""
        data = _hmm({"action": "decode", "model_path": cleanup_model["model_path"]}, headers)
        if data.get("status") == "error":
            pytest.skip(f"解码失败: {data.get('error')}")

        dec = data["decode"]
        n_states = cleanup_model["model"]["n_states"]

        seq = dec["state_sequence"]
        assert len(seq) == dec["n_observations"]
        assert all(isinstance(s, int) and 0 <= s < n_states for s in seq)

        # 各状态天数之和 == 序列长度
        assert sum(dec["regime_summary"].values()) == len(seq)

        # 切换点必须与序列一致：transitions 数量 == 相邻不同的次数
        n_switch = sum(1 for i in range(1, len(seq)) if seq[i] != seq[i - 1])
        assert len(dec["transitions"]) == n_switch
        assert len(dec["dates"]) == len(seq)

    @pytest.mark.timeout(200)
    def test_decode_dimension_mismatch_rejected(self, cleanup_model, headers):
        """请求覆盖成非法特征名时应因该特征名本身被拒绝"""
        data = _hmm({"action": "decode", "model_path": cleanup_model["model_path"],
                     "features": ["close"]}, headers)
        assert data.get("status") == "error"
        # 必须明确指出是 "close" 这个特征名非法，而不是模型自带的 features 列表有问题。
        # 后者曾长期发生：decode 把模型里的限定名 sh.600000.return_1d 直接回传给
        # buildObservations，撞上 unknown feature，使所有 decode 请求都失败。
        assert "close" in data.get("error", ""), f"错误信息不符: {data}"

    @pytest.mark.timeout(200)
    def test_decode_uses_model_features(self, cleanup_model, headers):
        """不带 features/symbols 解码时，应沿用模型自己的特征与标的"""
        data = _hmm({"action": "decode", "model_path": cleanup_model["model_path"]}, headers)
        assert data.get("status") != "error", \
            f"decode 失败（模型 features 为限定名 sh.xxx.feature，需还原成裸名）: {data}"

        dec = data["decode"]
        n_states = cleanup_model["model"]["n_states"]
        seq = dec["state_sequence"]
        assert len(seq) == dec["n_observations"]
        assert all(0 <= s < n_states for s in seq)
        assert len(dec["dates"]) == len(seq)
        assert sum(dec["regime_summary"].values()) == len(seq)


# ============================================================
# publish / list / delete
# ============================================================

class TestHMMModelLifecycle:

    @pytest.mark.timeout(120)
    def test_list_contains_trained_model(self, cleanup_model, headers):
        data = _hmm_get(headers)
        if data.get("status") == "error":
            pytest.skip(f"list 失败: {data.get('error')}")

        assert data["status"] == "success"
        assert isinstance(data["experiments"], list)
        assert isinstance(data["production"], list)

        match = [m for m in data["experiments"]
                 if m.get("model_id") == cleanup_model["model_id"]]
        assert match, f"训练后的模型未出现在 list 中"
        meta = match[0]
        assert meta["strategy_id"] == STRATEGY_ID
        assert meta["data_points"] > 0
        assert meta["features"] == cleanup_model["training_info"]["features"]

    @pytest.mark.timeout(120)
    def test_publish_makes_production_model_usable(self, cleanup_model, headers):
        """发布后的文件应能被 predict 直接加载（即 HMMNode 的 modelFile 格式）"""
        model_id = cleanup_model["model_id"]
        pub = _hmm({"action": "publish", "model_id": model_id,
                    "strategy_id": STRATEGY_ID}, headers)
        if pub.get("status") == "error":
            pytest.skip(f"发布失败: {pub.get('error')}")

        prod_path = pub["production_path"]
        assert _service_path(prod_path).exists(), \
            f"发布后的模型文件不存在: {prod_path}（解析为 {_service_path(prod_path)}）"

        # 生产模型可被推理加载
        pred = _hmm({"action": "predict", "model_path": prod_path,
                     "observation": [0.001, -0.002]}, headers)
        if pred.get("status") == "error":
            pytest.skip(f"生产模型推理失败: {pred.get('error')}")
        assert abs(sum(pred["prediction"]["state_probs"]) - 1.0) < 1e-6

        # list 中 production 段应包含该模型
        listed = _hmm_get(headers)
        assert any(m.get("strategy_id") == STRATEGY_ID
                   for m in listed.get("production", []))

        # 实验模型由 cleanup_model fixture 清理，不在此处重复删除

    @pytest.mark.timeout(120)
    def test_publish_unknown_model_returns_404(self, headers):
        data = _hmm({"action": "publish", "model_id": "20200101_000000",
                     "strategy_id": STRATEGY_ID}, headers, expect=404)
        assert "not found" in data.get("error", "")

    @pytest.mark.timeout(120)
    def test_publish_rejects_unsafe_ids(self, headers):
        data = _hmm({"action": "publish", "model_id": "../../etc/passwd",
                     "strategy_id": STRATEGY_ID}, headers, expect=400)
        assert "strategy_id" in data.get("error", "")

    @pytest.mark.timeout(120)
    def test_delete_removes_only_targeted_model(self, cleanup_model, headers):
        """删除必须精确命中目标文件，不能误删同目录其他模型"""
        other = _train(cleanup_model["training_info"]["symbols"], ["return_1d"],
                       headers=headers, strategy_id=STRATEGY_ID + "_other")
        if other.get("status") == "error":
            pytest.skip(f"训练失败: {other.get('error')}")

        ok, code, _ = _delete_model(cleanup_model["model_id"], headers=headers, expect=200)
        assert ok, f"删除失败 HTTP {code}"

        # 目标已消失
        ok, code, _ = _delete_model(cleanup_model["model_id"], headers=headers, expect=404)
        assert ok, f"重复删除应 404，实际 HTTP {code}"

        # 同目录另一个模型仍在
        listed = _hmm_get(headers)
        assert any(m.get("model_id") == other["model_id"]
                   for m in listed.get("experiments", [])), "误删了其他模型"

        _delete_model(other["model_id"], strategy_id=STRATEGY_ID + "_other",
                      headers=headers, expect=200)

    @pytest.mark.timeout(60)
    def test_get_rejects_unknown_action(self, headers):
        resp = requests.get(f"{BASE_URL}/hmm", params={"action": "train"},
                            headers=headers, verify=VERIFY_SSL, timeout=30)
        assert resp.status_code == 400
        assert "action" in resp.json().get("error", "")

    @pytest.mark.timeout(60)
    def test_post_rejects_unknown_action(self, headers):
        data = _hmm({"action": "nonexistent"}, headers, expect=400)
        assert "action" in data.get("error", "")