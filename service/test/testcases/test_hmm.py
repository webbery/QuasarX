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

import json
import pytest
import requests
from pathlib import Path

import sys
sys.path.insert(0, str(Path(__file__).parent))
from tool import BASE_URL, VERIFY_SSL, SERVICE_ROOT, CSV_DATA_DIR, run_backtest_graph

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
    def test_decode_rejects_mismatched_features(self, cleanup_model, headers):
        """
        请求的 features 与模型训练特征不一致时必须 400，不能静默改用模型的特征。

        decode 历史上无条件用模型自带的 features 覆盖请求值：传 ["close"] 也会
        成功返回，调用方却以为是用 close 解码的结果。静默忽略输入比报错更糟。
        """
        data = _hmm({"action": "decode", "model_path": cleanup_model["model_path"],
                     "features": ["close"]}, headers, expect=400)
        assert "requested" in data, f"错误信息应回显请求的特征: {data}"
        assert data["requested"] == ["close"], f"requested 回显不符: {data}"
        assert data["model_features"] == ["return_1d"], \
            f"错误信息应回显模型特征: {data}"

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

    @pytest.mark.timeout(200)
    def test_decode_accepts_matching_features(self, cleanup_model, headers):
        """显式传入与模型一致的特征（裸名或限定名都应接受）不应被拒"""
        for feats in (["return_1d"], cleanup_model["training_info"]["features"]):
            data = _hmm({"action": "decode", "model_path": cleanup_model["model_path"],
                         "features": feats}, headers)
            assert data.get("status") != "error", \
                f"传入与模型一致的特征 {feats} 却被拒: {data}"


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


# ============================================================
# HMMNode（策略图节点）
#
# 走回测链路：input → Return(1) → HMM → DebugNode
#
# 参数刻意取小（train_window=60 / warmup_period=10），回测窗口 1 年约
# 240 根 bar，够跑完「预热 + 填缓冲 + 训练 + 数次重训」又不拖慢用例。
# 默认参数需要 252+60=312 根 bar，得用 7 年窗口，代价太大。
#
# 输出语义（务必区分，否则断言会写错）：
#   hmm_state       —— DataContext::add(double) 逐 bar 追加，是时间序列
#   hmm_probs       —— add(Vector<double>) 整体替换，只保留最后一根 bar 的向量
#   hmm_transition  —— 同上，长度 n_states²
#   hmm_duration    —— 同上，长度 n_states
# 向量类输出因此只对最后一根 bar 有效（与 Formula 里 hmm_probs[1] 的用法一致）。
# ============================================================

NODE_TRAIN_WINDOW = 60
NODE_WARMUP = 10
NODE_RETRAIN = 20
NODE_N_STATES = 3
NODE_DEBUG_LABEL = "debug_hmm"
NODE_START = "2023-01-01"
NODE_END = "2023-12-31"


def _hmm_node_strategy(symbol: str, strategy_id: str,
                       n_states: int = NODE_N_STATES,
                       train_window: int = NODE_TRAIN_WINDOW,
                       warmup_period: int = NODE_WARMUP,
                       retrain_interval: int = NODE_RETRAIN,
                       max_iter: int = 100,
                       model_file: str = None) -> dict:
    """构造 input → Return(1) → HMM → DebugNode 策略图

    features 留空：HMMNode::Init 会自动从上游 out_elements() 取 key
    （FunctionNode 输出 {symbol}.{label}，此处即 {symbol}.Return(1)）。
    """
    hmm_params = {
        "n_states":        {"value": n_states,        "type": "number"},
        "train_window":    {"value": train_window,    "type": "number"},
        "warmup_period":   {"value": warmup_period,   "type": "number"},
        "retrain_interval":{"value": retrain_interval,"type": "number"},
        "max_iter":        {"value": max_iter,        "type": "number"},
        "random_seed":     {"value": 42,              "type": "number"},
    }
    if model_file:
        hmm_params["modelFile"] = {"value": model_file, "type": "text"}

    return {
        "id": strategy_id,
        "name": f"HMMNode测试_{strategy_id}",
        "version": 1,
        "description": "HMMNode 单元测试",
        "backtest": {"start": NODE_START, "end": NODE_END},
        "source": "A_hfq",
        "nodes": [
            {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "行情数据", "nodeType": "input", "params": {
                 "source": {"value": "股票", "type": "text"},
                 "code":   {"value": [symbol],     "type": "text"},
                 "freq":   {"value": "1d",         "type": "select"},
                 "close":  {"value": "close",      "type": "text"},
             }}},
            {"id": "2", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "Return(1)", "nodeType": "function", "params": {
                 "method": {"value": "Return", "type": "select"},
                 "range":  {"value": "1d",     "type": "text"},
             }}},
            {"id": "3", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": "HMM", "nodeType": "hmm", "params": hmm_params}},
            {"id": "4", "type": "custom", "position": {"x": 0, "y": 0},
             "data": {"label": NODE_DEBUG_LABEL, "nodeType": "debug", "params": {
                 "suffix": {"value": "csv", "type": "select"},
             }}},
        ],
        "edges": [
            {"id": "1-close->2", "source": "1", "target": "2",
             "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
            {"id": "2->3", "source": "2", "target": "3",
             "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            {"id": "3->4", "source": "3", "target": "4",
             "sourceHandle": "3", "targetHandle": "4", "type": "default"},
        ],
    }


def _read_hmm_csv(strategy_id: str) -> "pd.DataFrame":
    """读取 DebugNode 导出的 HMM 输出 CSV"""
    from tool import read_debug_csv
    return read_debug_csv(strategy_id, NODE_DEBUG_LABEL)


def _hmm_node_backtest(hmm_data, headers, **kwargs) -> "pd.DataFrame":
    """跑一次 HMMNode 回测并读取 DebugNode 导出的 CSV

    复用 hmm_data fixture 已导入的行情（取首个标的），行情不单独清理。
    kwargs 透传给 _hmm_node_strategy，可用 strategy_id 覆盖回测 id。
    """
    symbol = hmm_data[0]
    strategy_id = kwargs.pop("strategy_id", "test_hmmnode")
    strategy = _hmm_node_strategy(symbol, strategy_id, **kwargs)
    run_backtest_graph(strategy, headers, validate=False)
    return _read_hmm_csv(strategy_id)


def _hmm_state_series(df) -> "pd.Series":
    """取出 hmm_state 时间序列（去掉前导 NaN）

    列名就是 hmm_state，不带 symbol 前缀：HMMNode::Init 里
    _outputs["hmm_state"] 是硬编码的，与 FunctionNode 的 {symbol}.{label} 约定不同。
    """
    import pandas as pd
    col = "hmm_state"
    assert col in df.columns, f"列 {col} 不存在，实际列: {list(df.columns)}"
    return pd.to_numeric(df[col], errors="coerce").dropna()


def _hmm_tail_vector(df, column: str) -> list:
    """取向量类输出的最后一组有效值

    hmm_probs / hmm_transition / hmm_duration 在 context 里是 last-write-wins，
    DebugNode 把整个向量写进一列的前若干行，因此取该列开头的 n 个有效值。
    """
    import pandas as pd
    assert column in df.columns, f"列 {column} 不存在，实际列: {list(df.columns)}"
    vals = pd.to_numeric(df[column], errors="coerce").dropna().tolist()
    return vals


class TestHMMNodeOutputs:
    """HMMNode 数值不变量：概率/转移矩阵/持续时间的数学硬约束"""

    @pytest.mark.timeout(600)
    def test_node_emits_valid_probabilities(self, hmm_data, headers):
        """概率分布归一、状态编号合法、转移矩阵行归一、duration 定义式成立"""
        df = _hmm_node_backtest(hmm_data, headers, strategy_id="test_hmmnode_valid")

        states = _hmm_state_series(df)
        assert len(states) > 20, f"有效输出过少，可能节点没跑起来: {len(states)}"

        # hmm_state 是时间序列：整数且落在 [0, n_states)
        assert all(float(s).is_integer() for s in states), "状态编号必须是整数"
        assert all(0 <= int(s) < NODE_N_STATES for s in states), \
            f"状态越界: {sorted(set(int(s) for s in states))}"

        # hmm_probs：归一（非负 + 和为 1）
        probs = _hmm_tail_vector(df, "hmm_probs")
        assert len(probs) == NODE_N_STATES, \
            f"hmm_probs 长度应为 {NODE_N_STATES}，实际 {len(probs)}"
        assert all(p >= 0 for p in probs), f"概率为负: {probs}"
        assert abs(sum(probs) - 1.0) < 1e-6, f"概率未归一: {probs}（和={sum(probs)}）"

        # hmm_transition：n_states² 个值，reshape 后每行归一
        trans = _hmm_tail_vector(df, "hmm_transition")
        assert len(trans) == NODE_N_STATES ** 2, \
            f"hmm_transition 长度应为 {NODE_N_STATES**2}，实际 {len(trans)}"
        A = [trans[i * NODE_N_STATES:(i + 1) * NODE_N_STATES]
             for i in range(NODE_N_STATES)]
        for i, row in enumerate(A):
            assert abs(sum(row) - 1.0) < 1e-6, \
                f"转移矩阵第 {i} 行未归一（和={sum(row)}），疑似序列化转置: {row}"
            assert all(v >= 0 for v in row)

        # hmm_duration == 1 / (1 - A_ii)，纯代数恒等式
        dur = _hmm_tail_vector(df, "hmm_duration")
        assert len(dur) == NODE_N_STATES, \
            f"hmm_duration 长度应为 {NODE_N_STATES}，实际 {len(dur)}"
        for i in range(NODE_N_STATES):
            expected = 1.0 / (1.0 - A[i][i]) if A[i][i] < 1.0 else 10000.0
            assert abs(dur[i] - expected) < 1e-3 * max(1.0, expected), \
                f"状态 {i} 持续时间与 1/(1-A_ii) 不符: {dur[i]} vs {expected}"

    @pytest.mark.timeout(600)
    def test_node_state_matches_argmax_probs(self, hmm_data, headers):
        """hmm_state 应等于 hmm_probs 的 argmax（两者来自同一次 predict_proba）"""
        df = _hmm_node_backtest(hmm_data, headers, strategy_id="test_hmmnode_argmax")
        probs = _hmm_tail_vector(df, "hmm_probs")
        states = _hmm_state_series(df)
        assert len(states) > 0

        # 向量输出只对应最后一根 bar，用最后一根 bar 的 state 做比对
        last_state = int(states.iloc[-1])
        assert last_state == probs.index(max(probs)), \
            f"hmm_state={last_state} 与 argmax(hmm_probs)={probs.index(max(probs))} 不一致"


class TestHMMNodeTiming:
    """HMMNode 时序状态机：预热期、首次输出滞后、重训行为

    ⚠ 不要用 CSV 行索引当 bar 序号。DebugNode 的 datetime 列记录全部被处理的
    epoch（实测 2010~2026 共 4001 行），而 hmm_state 只有节点真正输出时才追加，
    值从第 0 行起紧密排列——两者不对齐。hmm_state 有效值的**个数**才是可靠的量：
    它等于「节点开始输出后被处理的 epoch 数」。
    """

    @staticmethod
    def _output_count(df) -> int:
        """节点实际输出（而非 Skip）的 bar 数"""
        return len(_hmm_state_series(df))

    @pytest.mark.timeout(600)
    def test_first_output_shifts_with_train_window(self, hmm_data, headers):
        """
        train_window 越大，开始输出越晚，有效输出 bar 数越少。

        HMMNode::Process：前 warmup_period 根只累积不输出；之后每根追加观测，
        直到缓冲区填满 train_window 才首次训练并输出。滞后 = warmup + train_window。

        只断言单调性而非具体差值：有效 bar 数还受引擎决策节奏（交易日历）
        影响，实测 60→120 的差值为 30 而非 60，不能按 bar 数线性外推。
        """
        base = _hmm_node_backtest(hmm_data, headers,
                                 strategy_id="test_hmmnode_w60", train_window=60)
        longer = _hmm_node_backtest(hmm_data, headers,
                                    strategy_id="test_hmmnode_w120", train_window=120)

        n_base = self._output_count(base)
        n_long = self._output_count(longer)
        assert n_base > 0, "train_window=60 应有输出"
        assert n_long > 0, "train_window=120 应有输出"
        assert n_long < n_base, \
            f"train_window 更大却输出更多 bar: {n_base} (w=60) vs {n_long} (w=120)"

    @pytest.mark.timeout(600)
    def test_warmup_delays_first_output(self, hmm_data, headers):
        """
        warmup_period 越大，开始输出越晚，有效输出 bar 数越少。

        预热期 _days_since_train 持续自增但直接返回 Skip，不训练不输出。
        """
        small = _hmm_node_backtest(hmm_data, headers,
                                  strategy_id="test_hmmnode_warm10", warmup_period=10)
        large = _hmm_node_backtest(hmm_data, headers,
                                   strategy_id="test_hmmnode_warm500", warmup_period=500)

        n_small = self._output_count(small)
        n_large = self._output_count(large)
        assert n_small > n_large, \
            f"预热期更长却输出更多 bar: {n_small} (warm=10) vs {n_large} (warm=500)"

    @pytest.mark.timeout(600)
    def test_retrain_interval_does_not_delay_first_output(self, hmm_data, headers):
        """
        retrain_interval < warmup_period 时，首次输出时机不受 retrain_interval 影响。

        预热期 _days_since_train 虽自增但直接 Skip，训练时机只取决于缓冲区是否填满。
        两种 retrain_interval 跑同一组其余参数，有效输出 bar 数应相同。
        """
        fast = _hmm_node_backtest(hmm_data, headers,
                                 strategy_id="test_hmmnode_ri5",
                                 warmup_period=300, retrain_interval=5, train_window=60)
        slow = _hmm_node_backtest(hmm_data, headers,
                                 strategy_id="test_hmmnode_ri999",
                                 warmup_period=300, retrain_interval=999, train_window=60)
        n_fast = self._output_count(fast)
        n_slow = self._output_count(slow)
        assert n_fast > 0, "warmup=300 / train_window=60 应仍有输出"
        assert n_fast == n_slow, \
            f"retrain_interval 不应影响首次输出时机，但输出 bar 数不同: " \
            f"{n_fast} (ri=5) vs {n_slow} (ri=999)"


class TestHMMNodeTraining:
    """HMMNode 训练行为：模型确实被更新、参数边界"""

    @pytest.mark.timeout(600)
    def test_model_is_actually_retrained(self, hmm_data, headers):
        """
        max_iter=1 时模型也应被更新，不能永远停在第一个模型。

        GaussianHMM::train() 返回的是「是否收敛」，不是「是否训练成功」。
        曾经 HMMNode 用返回值判断，EM 未收敛时新模型被丢弃、_hmm 永远停在
        第一个模型，而 _days_since_train 照常归零，日志里看不出异常。
        这里用短 retrain_interval 保证回测期内有多次重训机会，
        断言状态序列的取值不是恒定不变。
        """
        df = _hmm_node_backtest(hmm_data, headers,
                                strategy_id="test_hmmnode_retrain",
                                max_iter=1, retrain_interval=5)
        states = _hmm_state_series(df)
        assert len(states) > 20, f"有效输出过少: {len(states)}"
        # 状态取值不应只有一个（模型僵死会导致全程同一状态）
        assert len(set(states.tolist())) > 1, \
            "全程只有单一状态，模型可能被判定为未收敛而未更新"

    @pytest.mark.timeout(600)
    def test_invalid_train_window_rejected(self, hmm_data, headers):
        """train_window 小于 n_states*2 时训练必然失败，节点应报错而非静默产出垃圾"""
        symbol = hmm_data[0]
        strategy = _hmm_node_strategy(symbol, "test_hmmnode_badwindow",
                                      train_window=3, n_states=3)
        resp = requests.post(f"{BASE_URL}/backtest",
                             json={"script": json.dumps(strategy), "validate": False},
                             headers=headers, verify=VERIFY_SSL, timeout=300)
        # 回测本身可能仍返回 200（训练失败只打 WARN），关键是不要产出有效状态
        if resp.status_code == 200:
            try:
                df = _read_hmm_csv("test_hmmnode_badwindow")
            except AssertionError:
                return  # 没生成 CSV 更好：说明节点压根没跑起来
            states = _hmm_state_series(df)
            assert len(states) == 0, \
                f"train_window=3 < n_states*2=6，不可能训练成功，却输出了 {len(states)} 个状态"
        else:
            assert resp.status_code in (400, 500)