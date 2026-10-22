#!/usr/bin/env python3
"""特征筛选门禁 API 测试（/v0/ml action=screen）。

覆盖范围
--------
1. 参数校验：缺 csv_path、文件不存在、next_open 缺 OHLCV、规则越界、label 非对象
2. 端到端：提交 → train_status → summary/files → screen_result
3. 门禁结论：合成信号必须进 A 档，纯噪声必须不入选
4. 滞后敏感性：慢变信号判 persistent、快变信号判 decaying
5. 参数透传：min_ic / label.period / features / no_lag_sensitivity / trial_count
6. entry=next_open（配套合成 OHLCV）
7. 输出形态：服务端 --no-plot（无 PNG）、文件落盘、screen_result 与文件一致

测试数据
--------
不依赖行情源，也不依赖 collect：screen 的唯一输入就是一份特征缓存 CSV。
``generate_screen_feature_cache`` 种入三个已知特征：
  - sig   AR(1) φ=0.97，驱动未来收益 → A 档 + persistent
  - fast  iid，只驱动次日收益       → A 档 + decaying
  - noise 与收益无关                 → 不入选

前置：服务已启动（conftest 提供 auth_token），服务端 Python 环境需有 pandas。
"""

import json
import os
import shutil
import sys
import time
from pathlib import Path

import pytest
import requests
import urllib3

from generate_test_data import (generate_screen_feature_cache,
                                generate_screen_ohlcv)

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

BASE_URL = "https://localhost:19107/v0"
VERIFY_SSL = False

# 模块内创建过的 run_id，teardown 时只删除这些目录
_CREATED_RUNS = []


def _headers(token):
    return {"Authorization": token}


def _merge(base: dict, override: dict) -> dict:
    """递归合并嵌套 dict（label / rules 需要局部覆盖）。"""
    out = dict(base)
    for k, v in override.items():
        if isinstance(v, dict) and isinstance(out.get(k), dict):
            out[k] = _merge(out[k], v)
        else:
            out[k] = v
    return out


def _submit(token, body, timeout=60):
    return requests.post(f"{BASE_URL}/ml", json=body,
                         headers=_headers(token), verify=VERIFY_SSL,
                         timeout=timeout)


def _poll(token, session_id, timeout=180.0):
    """轮询 train_status 直到 done/error。"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(0.5)
        r = requests.get(f"{BASE_URL}/ml",
                         params={"action": "train_status",
                                 "session_id": session_id},
                         headers=_headers(token), verify=VERIFY_SSL,
                         timeout=15)
        if r.status_code != 200:
            continue
        data = r.json()
        status = data.get("status")
        if status in ("done", "error"):
            return data
    pytest.fail(f"screen 超时（{timeout}s 未完成）")


def _detail(token, run_id):
    r = requests.get(f"{BASE_URL}/ml",
                     params={"action": "screen_result", "run_id": run_id},
                     headers=_headers(token), verify=VERIFY_SSL, timeout=60)
    assert r.status_code == 200, r.text[:400]
    return r.json()


def _run_screen(token, py_env, panel, timeout=180.0, **overrides):
    """提交一次 screen 并返回最终结果。

    缺省参数贴近生产默认；overrides 支持嵌套覆盖 label / rules。
    """
    body = {
        "action": "screen",
        "csv_path": panel["path"],
        "py_env": py_env,
        "dev_end": panel["dev_end"],
        "test_start": panel["test_start"],
        # 不传 primary_horizon：让它由 label.period 决定，
        # 这样透传测试才能验证 label.period 的映射关系
        "label": {"period": 5, "vol_k": 0.34, "sigma_mode": "expanding"},
        "horizons": [1, 5, 10, 20],
        "entry": "close",
        "n_folds": 5,
        "min_names": 15,
        "trial_count": 0,
        "rules": {"min_ic": 0.02, "min_t": 2.5, "min_sign_folds": 4,
                  "fdr_q": 0.10, "min_coverage": 0.5, "gate_topk": 5},
    }
    body = _merge(body, overrides)

    resp = _submit(token, body)
    assert resp.status_code == 200, f"提交失败: {resp.status_code} {resp.text[:400]}"
    data = resp.json()
    session_id = data.get("session_id")
    assert session_id, f"缺少 session_id: {data}"

    # 服务端与 train/collect/optimize 共用单会话槽：若已有其他任务在跑，
    # 提交会返回那个 session。别去轮询它，直接 skip。
    if data.get("status") == "running" and not str(session_id).startswith("screen_"):
        pytest.skip(f"服务上有其他 ML 会话在跑: {session_id}")

    result = _poll(token, session_id, timeout)

    if result.get("status") == "error":
        err = str(result.get("error", ""))
        if "No module named" in err or "ModuleNotFoundError" in err:
            pytest.skip(f"服务端 Python 环境缺少依赖: {err[:200]}")
        pytest.fail(f"screen 失败: {err[:600]}")

    if result.get("run_id"):
        _CREATED_RUNS.append(result["run_id"])
    return result


# ============== Fixtures ==============

@pytest.fixture(scope="module")
def py_env():
    """服务端 Python 解释器。

    默认取当前 pytest 的解释器：CI 的 setup-python 与测试依赖一起装了
    pandas，本地通常跑在 anaconda 下。可用 QS_TEST_PY_ENV 显式覆盖。
    """
    return os.environ.get("QS_TEST_PY_ENV", sys.executable)


@pytest.fixture(scope="module")
def panel(tmp_path_factory):
    """生成一份带已知信号的合成特征缓存 + 配套 OHLCV。"""
    d = tmp_path_factory.mktemp("screen_panel")
    meta = generate_screen_feature_cache(d / "feature_cache.csv")
    meta["ohlcv"] = generate_screen_ohlcv(d / "ohlcv.csv", meta["path"])
    return meta


@pytest.fixture(scope="module")
def main_run(auth_token, py_env, panel):
    """默认参数下的一次完整筛选（含 TEST / 增量 IC / 滞后敏感性）。"""
    return _run_screen(auth_token, py_env, panel)


@pytest.fixture(scope="module")
def main_detail(auth_token, main_run):
    return _detail(auth_token, main_run["run_id"])


@pytest.fixture(scope="module", autouse=True)
def cleanup_screen_runs():
    """只删除本模块创建的 run 目录，不去扫 screen_*，避免误删真实数据。"""
    yield
    if not _CREATED_RUNS:
        return
    try:
        from tool import _resolve_data_dir
        root = _resolve_data_dir() / "reports" / "feature_screen"
    except Exception:
        return
    for run_id in _CREATED_RUNS:
        target = root / run_id
        try:
            if target.is_dir():
                shutil.rmtree(target)
        except Exception:
            pass


# ============== 错误场景 ==============

class TestScreenErrors:
    """不依赖筛选结果的参数校验。"""

    def test_missing_csv_path(self, auth_token):
        resp = _submit(auth_token, {"action": "screen"})
        assert resp.status_code == 400, resp.text[:300]

    def test_nonexistent_csv(self, auth_token, tmp_path):
        resp = _submit(auth_token, {
            "action": "screen",
            "csv_path": str(tmp_path / "no_such_file.csv"),
        })
        assert resp.status_code == 400, resp.text[:300]

    def test_next_open_requires_ohlcv(self, auth_token, panel):
        resp = _submit(auth_token, {
            "action": "screen",
            "csv_path": panel["path"],
            "entry": "next_open",
        })
        assert resp.status_code == 400, resp.text[:300]
        assert "ohlcv" in resp.text.lower()

    def test_invalid_fdr_q(self, auth_token, panel):
        resp = _submit(auth_token, {
            "action": "screen",
            "csv_path": panel["path"],
            "rules": {"fdr_q": 1.5},
        })
        assert resp.status_code == 400, resp.text[:300]

    def test_min_sign_folds_exceeds_n_folds(self, auth_token, panel):
        resp = _submit(auth_token, {
            "action": "screen",
            "csv_path": panel["path"],
            "n_folds": 3,
            "rules": {"min_sign_folds": 5},
        })
        assert resp.status_code == 400, resp.text[:300]

    def test_label_must_be_object(self, auth_token, panel):
        resp = _submit(auth_token, {
            "action": "screen",
            "csv_path": panel["path"],
            "label": "not-an-object",
        })
        assert resp.status_code == 400, resp.text[:300]

    def test_screen_result_rejects_path_traversal(self, auth_token):
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "screen_result", "run_id": "screen_../../etc"},
            headers=_headers(auth_token), verify=VERIFY_SSL, timeout=15)
        assert resp.status_code == 400, resp.text[:300]

    def test_screen_result_unknown_run(self, auth_token):
        resp = requests.get(
            f"{BASE_URL}/ml",
            params={"action": "screen_result",
                    "run_id": "screen_19700101_000000_0000"},
            headers=_headers(auth_token), verify=VERIFY_SSL, timeout=15)
        assert resp.status_code == 404, resp.text[:300]


# ============== 端到端 ==============

class TestScreenEndToEnd:
    """默认参数下的完整流程与门禁结论。"""

    def test_status_and_summary(self, main_run):
        assert main_run["status"] == "done", main_run
        assert main_run["run_id"].startswith("screen_")
        assert main_run["out_dir"]

        files = main_run["files"]
        assert isinstance(files, dict) and files, main_run
        assert set(files) == {"json", "csv", "md"}, files
        # 服务端固定 --no-plot：不返回任何图片路径
        assert not any("png" in k.lower() for k in files)

        summary = main_run["summary"]
        assert summary["n_features"] == 3, summary
        assert summary["m_tests"] == 3, summary      # trial_count=0 → M=候选数
        assert summary["primary_horizon"] == 5
        assert summary["plots"] is False
        assert summary["dev_days"] > 0 and summary["test_days"] > 0

    def test_files_written(self, main_run):
        for kind, path in main_run["files"].items():
            p = Path(path)
            assert p.exists(), f"{kind} 未落盘: {path}"
            assert p.stat().st_size > 0, f"{kind} 是空文件: {path}"

    def test_signal_is_tier_a_noise_rejected(self, main_detail):
        feats = main_detail["features"]
        assert set(feats) == {"sig", "fast", "noise"}, sorted(feats)

        sig = feats["sig"]
        assert sig["dev"]["ic_mean"] > 0, sig["dev"]
        assert abs(sig["dev"]["nw_t"]) > 2.5, sig["dev"]
        assert sig["selection"]["selected"] is True, sig["selection"]
        assert sig["selection"]["tier"] == "A", sig["selection"]

        noise = feats["noise"]
        assert noise["selection"]["selected"] is False, noise["selection"]

    def test_lag_sensitivity_verdicts(self, main_detail):
        feats = main_detail["features"]
        # sig 是 φ=0.97 的慢变量：用前一天的值几乎不损失信息
        assert feats["sig"]["lag"]["verdict"] == "persistent", \
            feats["sig"]["lag"]
        # fast 是 iid：晚一天就完全错过标签窗口
        assert feats["fast"]["lag"]["verdict"] == "decaying", \
            feats["fast"]["lag"]

    def test_lag_fields_present_in_main_run(self, main_detail):
        """主运行开启了滞后敏感性，字段必须存在。"""
        assert "lag" in main_detail["features"]["sig"]

    def test_screen_result_matches_file(self, main_run, main_detail, panel):
        on_disk = json.loads(Path(main_run["files"]["json"]).read_text())
        assert set(on_disk["features"]) == set(main_detail["features"])
        assert on_disk["meta"] == main_detail["meta"]
        # 输入路径与请求一致
        assert on_disk["meta"]["feature_cache"] == panel["path"]


# ============== 参数透传 ==============

class TestScreenParamPlumbing:
    """验证 C++ 侧拼的 CLI 参数真的到达了脚本。

    arg 拼错不会报错，只会悄悄算出不同的结果——这组用例专门抓这类静默错误。
    """

    def test_min_ic_blocks_all(self, auth_token, py_env, panel):
        """min_ic=0.9 时任何特征都不应入选（证明 rules 真的透传）。"""
        run = _run_screen(
            auth_token, py_env, panel,
            features=["sig"],
            rules={"min_ic": 0.9},
            no_test=True, no_incremental=True, no_lag_sensitivity=True,
        )
        detail = _detail(auth_token, run["run_id"])
        assert detail["features"]["sig"]["selection"]["selected"] is False
        assert run["summary"]["n_tier_a"] == 0

    def test_features_filter_and_no_lag(self, auth_token, py_env, panel):
        """features 过滤生效，且 no_lag_sensitivity 会去掉 lag 段。"""
        run = _run_screen(
            auth_token, py_env, panel,
            features=["sig"],
            no_test=True, no_incremental=True, no_lag_sensitivity=True,
        )
        assert run["summary"]["n_features"] == 1, run["summary"]
        detail = _detail(auth_token, run["run_id"])
        assert set(detail["features"]) == {"sig"}
        assert not detail["features"]["sig"].get("lag"), \
            detail["features"]["sig"].get("lag")
        assert run["summary"]["lag_sensitivity"] is False

    def test_label_period_and_trial_count(self, auth_token, py_env, panel):
        """label.period 映射到主 horizon；trial_count 进入 FDR 的 M。"""
        run = _run_screen(
            auth_token, py_env, panel,
            label={"period": 1},
            trial_count=50,
            features=["sig"],
            no_test=True, no_incremental=True, no_lag_sensitivity=True,
        )
        assert run["summary"]["primary_horizon"] == 1, run["summary"]
        assert run["summary"]["m_tests"] == 50, run["summary"]
        assert run["summary"]["bonferroni_t"] > 2.5, run["summary"]

    def test_next_open_entry(self, auth_token, py_env, panel):
        """entry=next_open + OHLCV 能跑通，且 meta.entry 正确回显。"""
        run = _run_screen(
            auth_token, py_env, panel,
            entry="next_open",
            ohlcv_cache=panel["ohlcv"],
            features=["sig"],
            no_test=True, no_incremental=True, no_lag_sensitivity=True,
        )
        assert run["status"] == "done"
        assert run["meta"]["entry"] == "next_open", run["meta"]
        detail = _detail(auth_token, run["run_id"])
        assert detail["features"]["sig"]["dev"]["ic_mean"] > 0
        assert detail["features"]["sig"]["dev"]["coverage"] > 0.5


# ============== 折间间隔（gap） ==============

class TestFoldGap:
    """折间间隔参数：生效值、折日期边界、容量校验。"""

    def test_gap_floored_to_horizon(self, auth_token, py_env, panel):
        """fold_gap=0 时生效值 = primary_horizon。"""
        run = _run_screen(
            auth_token, py_env, panel,
            fold_gap=0,
            label={"period": 5},
            features=["sig"],
            no_test=True, no_incremental=True, no_lag_sensitivity=True,
        )
        assert run["status"] == "done"
        assert run["meta"]["fold_gap"] == 0
        assert run["meta"]["effective_gap"] == 5
        detail = _detail(auth_token, run["run_id"])
        folds = detail["features"]["sig"]["dev"]["folds"]
        assert folds["gap"] == 5
        assert folds["effective_gap"] == 5
        assert folds["gap_raised"] is True

    def test_gap_explicit(self, auth_token, py_env, panel):
        """fold_gap=20 > horizon=5 时生效值 = 20，gap_raised=False。"""
        run = _run_screen(
            auth_token, py_env, panel,
            fold_gap=20,
            label={"period": 5},
            features=["sig"],
            no_test=True, no_incremental=True, no_lag_sensitivity=True,
        )
        assert run["status"] == "done"
        assert run["meta"]["fold_gap"] == 20
        assert run["meta"]["effective_gap"] == 20
        detail = _detail(auth_token, run["run_id"])
        folds = detail["features"]["sig"]["dev"]["folds"]
        assert folds["gap"] == 20
        assert folds["effective_gap"] == 20
        assert folds["gap_raised"] is False

    def test_gap_between_folds(self, auth_token, py_env, panel):
        """相邻折的日期边界间隔恰好 = effective_gap 根 bar。"""
        from datetime import datetime, timedelta
        run = _run_screen(
            auth_token, py_env, panel,
            fold_gap=10,
            label={"period": 5},
            n_folds=5,
            features=["sig"],
            no_test=True, no_incremental=True, no_lag_sensitivity=True,
        )
        assert run["status"] == "done"
        detail = _detail(auth_token, run["run_id"])
        folds = detail["features"]["sig"]["dev"]["folds"]
        dates = folds["fold_dates"]
        assert len(dates) == 5
        # 折 i 的结束日期与折 i+1 的开始日期之间间隔恰好 = effective_gap 根 bar
        for i in range(len(dates) - 1):
            end_i = datetime.strptime(dates[i][1].split()[0], "%Y-%m-%d")
            start_next = datetime.strptime(dates[i+1][0].split()[0], "%Y-%m-%d")
            # 工作日间隔（排除周末）
            gap_bars = 0
            d = end_i + timedelta(days=1)
            while d < start_next:
                if d.weekday() < 5:  # 周一到周五
                    gap_bars += 1
                d += timedelta(days=1)
            assert gap_bars == folds["effective_gap"], \
                f"fold {i+1}->{i+2} gap={gap_bars}, expected {folds['effective_gap']}"

    def test_gap_capacity_insufficient(self, auth_token, py_env, panel):
        """折数过高导致容量不足时，服务端应返回错误。"""
        resp = _submit(auth_token, {
            "action": "screen",
            "csv_path": panel["path"],
            "py_env": py_env,
            "dev_end": panel["dev_end"],
            "test_start": panel["test_start"],
            "label": {"period": 5},
            "horizons": [1, 5, 10, 20],
            "entry": "close",
            "n_folds": 50,  # 过高
            "fold_gap": 20,  # 过大
            "min_names": 15,
            "features": ["sig"],
            "no_test": True, "no_incremental": True, "no_lag_sensitivity": True,
        })
        assert resp.status_code == 200
        data = resp.json()
        session_id = data.get("session_id")
        if data.get("status") == "running" and not str(session_id).startswith("screen_"):
            pytest.skip(f"服务上有其他 ML 会话在跑: {session_id}")
        result = _poll(auth_token, session_id, timeout=60)
        # 应返回 error 状态，且错误信息包含容量不足
        assert result["status"] == "error"
        err = str(result.get("error", ""))
        assert "不足" in err or "capacity" in err.lower() or "too small" in err.lower()
