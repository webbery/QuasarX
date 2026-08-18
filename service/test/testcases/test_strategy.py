import requests
import sys
from pathlib import Path
from tool import check_response, BASE_URL, VERIFY_SSL, DEBUG_DIR
import pytest
import json
import os
import time

@pytest.mark.usefixtures("auth_token")
class TestStrategy:
    STRATEGY_NAME = 'test_ma_strategy'

    def _auth_kwargs(self, auth_token):
        kwargs = {'verify': False}
        if auth_token and len(auth_token) > 10:
            kwargs['headers'] = {'Authorization': auth_token}
        return kwargs

    def load_script(self, script_path):
        with open(script_path, 'r', encoding='utf-8') as file:
            return json.load(file)

    def get_all_strategies(self, auth_token):
        """获取所有策略列表"""
        kwargs = self._auth_kwargs(auth_token)
        response = requests.get(f"{BASE_URL}/strategy", **kwargs)
        return check_response(response)

    def find_strategy(self, status_list, name):
        """在状态列表中查找策略"""
        for s in status_list:
            if s.get('name') == name:
                return s
        return None

    def cleanup_strategy(self, auth_token, name):
        """清理策略（停止 + 删除），用于测试后恢复干净状态"""
        try:
            kwargs = self._auth_kwargs(auth_token)
            # 先停止
            kwargs['json'] = {'mode': 2, 'name': name}
            requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
            # 再删除
            kwargs['json'] = {'name': name}
            requests.delete(f"{BASE_URL}/strategy", **kwargs, timeout=5)
            time.sleep(0.5)
        except Exception:
            pass

    @pytest.mark.timeout(10)
    def test_upload_model(self, auth_token):
        kwargs = self._auth_kwargs(auth_token)

        model_path = "../models/lstm_model.onnx"
        if not os.path.exists(model_path):
            pytest.skip(f"模型文件不存在: {model_path}")

        with open(model_path, "rb") as f:
            files = {"file": ("lstm_model.onnx", f)}
            response = requests.put(
                f"{BASE_URL}/strategy/node",
                files=files,
                **kwargs
            )
        check_response(response)

    @pytest.mark.timeout(120)
    def test_strategy_lifecycle(self, auth_token, is_backtest):
        """完整生命周期测试：部署 → 停止 → 再运行 → 再停止 → 删除

        每个阶段后调用 GET /strategy 验证列表状态和返回格式。
        每次变更后等待 15s 确保行情数据到达并更新 epochCount。
        """
        if is_backtest:
            pytest.skip("回测模式下不支持策略生命周期操作（stop/run）")

        lifecycle_name = 'test_lifecycle'
        self.cleanup_strategy(auth_token, lifecycle_name)

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)
        kwargs = self._auth_kwargs(auth_token)

        def verify_strategy_list_format(strategies, expected_running=None, expect_epoch_count_increase=None):
            """验证策略列表格式：每项必须有 name、running、epochCount、lastHeartbeat 字段"""
            assert isinstance(strategies, list)
            target = None
            for item in strategies:
                assert 'name' in item, "策略列表项缺少 'name' 字段"
                assert 'running' in item, "策略列表项缺少 'running' 字段"
                assert isinstance(item['running'], bool), "'running' 字段应为布尔值"
                assert 'epochCount' in item, "策略列表项缺少 'epochCount' 字段"
                assert 'lastHeartbeat' in item, "策略列表项缺少 'lastHeartbeat' 字段"
                assert isinstance(item['epochCount'], int), "'epochCount' 字段应为整数"
                assert isinstance(item['lastHeartbeat'], int), "'lastHeartbeat' 字段应为整数"
                if item['name'] == lifecycle_name:
                    target = item

            if expected_running is not None and target is not None:
                assert target['running'] == expected_running, \
                    f"'{lifecycle_name}' 的 running 应为 {expected_running}"

            if expect_epoch_count_increase is not None and target is not None:
                prev_count, prev_ts = expect_epoch_count_increase
                assert target['epochCount'] >= prev_count, \
                    f"'{lifecycle_name}' 的 epochCount({target['epochCount']}) 不应小于之前的值({prev_count})"
                if target['running']:
                    assert target['lastHeartbeat'] >= prev_ts, \
                        f"运行中的 '{lifecycle_name}' lastHeartbeat({target['lastHeartbeat']}) 不应小于之前的值({prev_ts})"

            return target

        # 1. 部署
        kwargs['json'] = {'mode': 0, 'name': lifecycle_name, 'script': script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is True, "部署后策略应处于运行状态"

        # 验证部署后策略列表
        time.sleep(15)  # TickFlow 查询间隔为 10s + 网络延迟 + 数据发布，至少需要 15s 才能收到第一笔行情
        strategies = self.get_all_strategies(auth_token)
        found = verify_strategy_list_format(strategies, expected_running=True)
        assert found is not None, "部署的策略应在策略列表中"
        assert found['epochCount'] > 0, "运行中的策略 epochCount 应大于 0"
        assert found['lastHeartbeat'] > 0, "运行中的策略 lastHeartbeat 应大于 0"
        prev_epoch = found['epochCount']
        prev_heartbeat = found['lastHeartbeat']

        # 2. 停止
        kwargs['json'] = {'mode': 2, 'name': lifecycle_name}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is False, "停止后策略应处于停止状态"

        # 验证停止后策略列表
        time.sleep(15)
        strategies = self.get_all_strategies(auth_token)
        found = verify_strategy_list_format(strategies, expected_running=False,
                                            expect_epoch_count_increase=(prev_epoch, prev_heartbeat))
        assert found is not None, "停止后策略仍应在列表中"
        prev_epoch = found['epochCount']
        prev_heartbeat = found['lastHeartbeat']

        # 3. 再运行
        kwargs['json'] = {'mode': 1, 'name': lifecycle_name}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is True, "再运行后策略应处于运行状态"

        # 验证再运行后策略列表
        time.sleep(15)
        strategies = self.get_all_strategies(auth_token)
        found = verify_strategy_list_format(strategies, expected_running=True,
                                            expect_epoch_count_increase=(prev_epoch, prev_heartbeat))
        assert found is not None, "再运行后策略仍应在列表中"
        assert found['epochCount'] >= prev_epoch, "再运行后 epochCount 不应回退"
        prev_epoch = found['epochCount']
        prev_heartbeat = found['lastHeartbeat']

        # 4. 再停止
        kwargs['json'] = {'mode': 2, 'name': lifecycle_name}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is False, "再停止后策略应处于停止状态"

        # 验证再停止后策略列表
        time.sleep(15)
        strategies = self.get_all_strategies(auth_token)
        found = verify_strategy_list_format(strategies, expected_running=False,
                                            expect_epoch_count_increase=(prev_epoch, prev_heartbeat))
        assert found is not None, "再停止后策略仍应在列表中"

        # 5. 删除
        kwargs['json'] = {'name': lifecycle_name}
        resp = requests.delete(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['message'] == 'success', "删除应成功"

        # 验证删除后策略列表
        strategies = self.get_all_strategies(auth_token)
        verify_strategy_list_format(strategies)
        found = self.find_strategy(strategies, lifecycle_name)
        assert found is None, "删除的策略不应在策略列表中"

    @pytest.mark.timeout(30)
    def test_redeploy_overwrite(self, auth_token, is_backtest):
        """重复部署同名策略，应覆盖成功"""
        if is_backtest:
            pytest.skip("回测模式下不支持重复部署操作")

        redeploy_name = 'test_redeploy'
        self.cleanup_strategy(auth_token, redeploy_name)

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)
        kwargs = self._auth_kwargs(auth_token)

        # 第一次部署
        kwargs['json'] = {'mode': 0, 'name': redeploy_name, 'script': script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        check_response(resp)

        # 第二次部署（覆盖）
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['message'] == 'success', "重复部署应成功"
        assert data['name'] == redeploy_name
        assert data['running'] is True

        # 清理
        self.cleanup_strategy(auth_token, redeploy_name)

    @pytest.mark.timeout(10)
    def test_stop_nonexistent_strategy(self, auth_token, is_backtest):
        """停止不存在的策略，服务不应崩溃"""
        if is_backtest:
            pytest.skip("回测模式下不支持停止操作")

        kwargs = self._auth_kwargs(auth_token)
        kwargs['json'] = {'mode': 2, 'name': 'nonexistent_strategy_12345'}

        try:
            response = requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
            # 服务不崩溃即可，状态码可以是 400/404/500
            assert response.status_code in (200, 400, 404, 500)
        except requests.exceptions.RequestException:
            pytest.fail("停止不存在策略时服务无响应")

    @pytest.mark.timeout(600)
    def test_run_all_script(self, auth_token, is_backtest):
        """批量回测所有策略脚本，验证回测结果"""
        if not is_backtest:
            pytest.skip("批量回测仅在回测模式下运行")

        kwargs = self._auth_kwargs(auth_token)

        no_reply = ['ml.json']
        skip_scripts = ['ml.json', 'ETF_Momentum.json']  # 跳过不需要验证的策略
        script_dir = './script'
        for item_name in os.listdir(script_dir):
            if item_name in skip_scripts:
                continue

            item_path = os.path.join(script_dir, item_name)
            print('run script:', item_name)
            script = self.load_script(item_path)
            if len(script) < 2:
                return

            script_json = json.dumps(script, ensure_ascii=False)
            headers = {'Authorization': auth_token} if auth_token else {}
            files = {
                "script": ("script.json", script_json.encode("utf-8"), "application/json"),
            }
            response = requests.post(
                f"{BASE_URL}/backtest", files=files,
                headers=headers, verify=VERIFY_SSL, timeout=600,
            )
            data = check_response(response)
            assert isinstance(data, object)
            if item_name in no_reply:
                continue

            assert 'buy' in data
            assert len(data['buy']) > 0
            assert 'sell' in data
            assert len(data['sell']) > 0

            assert 'features' in data
            assert len(data['features']) > 0
            features = data['features']
            assert 'sharp' in features

    @pytest.mark.timeout(60)
    def test_tickflow_context_has_quote(self, auth_token, is_backtest):
        """验证 TickFlow 模式下 context 中有 quote 数据，策略能正常执行

        步骤:
        1. 部署 ma_graph_strategy（使用 sz.000001，在 TickFlow pool 中）
        2. 等待 2 个 TickFlow 周期（10s × 2）+ 5s 缓冲
        3. 验证策略正在运行且 epochCount > 0
        4. 验证 DebugNode 输出文件存在且非空
        """
        if is_backtest:
            pytest.skip("回测模式下跳过，仅在 TickFlow/实盘模式下运行")

        name = 'test_quote_context'
        self.cleanup_strategy(auth_token, name)

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)
        kwargs = self._auth_kwargs(auth_token)

        kwargs['json'] = {'mode': 0, 'name': name, 'script': script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        check_response(resp)

        # 等待 2 个 TickFlow 周期（10s × 2）+ 5s 缓冲
        time.sleep(25)

        # 验证策略正在运行
        strategies = self.get_all_strategies(auth_token)
        found = self.find_strategy(strategies, name)
        assert found is not None, "策略应在列表中"
        assert found['running'] is True, "策略应处于运行状态"
        assert found['epochCount'] > 0, f"策略应至少执行 1 个 epoch，实际 {found['epochCount']}"

        # 验证 DebugNode 输出文件存在（证明 context 中有 quote 数据，策略图成功执行）
        # DebugNode 输出路径: {database_path}/data/debug/{strategy}/{label}.csv
        debug_dir = DEBUG_DIR / name
        time.sleep(3)  # 等待文件写入
        assert debug_dir.exists(), f"Debug 输出目录不存在: {debug_dir}"
        csv_files = [f for f in os.listdir(debug_dir) if f.endswith('.csv')]
        assert len(csv_files) > 0, f"Debug 输出目录中无 CSV 文件: {debug_dir}"

        self.cleanup_strategy(auth_token, name)

    @pytest.mark.timeout(120)
    def test_tickflow_kbar_aggregation(self, auth_token, is_backtest):
        """验证 TickFlow 模式下 KBar 聚合功能

        步骤:
        1. 部署策略，设置 freq="1m"（1 分钟聚合）
        2. 等待 65s（60s 聚合窗口 + 5s 缓冲）
        3. 验证聚合完成后 epochCount >= 1

        注意: ma_graph_strategy.json 默认 freq="1d"，
        本测试需要策略中 QuoteInputNode 的 freq 参数为 "1m"。
        """
        if is_backtest:
            pytest.skip("回测模式下跳过，仅在 TickFlow/实盘模式下运行")

        name = 'test_kbar_agg'
        self.cleanup_strategy(auth_token, name)

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)
        kwargs = self._auth_kwargs(auth_token)

        # 修改 QuoteInputNode 的 freq 为 "1m"
        for node in script['graph']['nodes']:
            if node.get('data', {}).get('nodeType') == 'input':
                node['data']['params']['freq'] = {'value': '1m'}
                break

        kwargs['json'] = {'mode': 0, 'name': name, 'script': script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        check_response(resp)

        # 等待聚合窗口（60s + 5s 缓冲）
        time.sleep(65)

        strategies = self.get_all_strategies(auth_token)
        found = self.find_strategy(strategies, name)
        assert found is not None, "策略应在列表中"
        assert found['running'] is True, "策略应处于运行状态"
        assert found['epochCount'] >= 1, f"聚合完成后 epochCount 应 >= 1，实际 {found['epochCount']}"

        self.cleanup_strategy(auth_token, name)


# ============== Multipart 部署（含模型文件 + 跨策略共享校验） ==============

class TestStrategyMultipartDeploy:
    """multipart deploy：策略 JSON + 模型文件上传

    - 正常 multipart deploy 应返回 200
    - 跨策略引用校验：XGBoostNode.modelFile 指向其他策略 → 400 拒绝
    - 不带 multipart 的旧 JSON 部署路径仍然工作（向后兼容）
    """

    DEPLOY_NAME = "test_xgb_deploy"

    def _xgb_strategy(self, name, xgb_label="XGBTestNode", model_file=None):
        expected = f"production/{name}-{xgb_label}.json"
        if model_file is None:
            model_file = expected
        return {
            "id": name, "name": name, "version": 1, "source": "A_hfq",
            "nodes": [
                {"id": "1", "type": "custom", "position": {"x": 0, "y": 0},
                 "data": {"label": "行情", "nodeType": "input",
                          "params": {"code": {"value": ["sz.800001"], "type": "text"},
                                     "freq": {"value": "1d", "type": "select"}}}},
                {"id": "2", "type": "custom", "position": {"x": 200, "y": 0},
                 "data": {"label": "MA", "nodeType": "function",
                          "params": {"method": {"value": "MA", "type": "select"},
                                     "range": {"value": "5d", "type": "text"}}}},
                {"id": "3", "type": "custom", "position": {"x": 400, "y": 0},
                 "data": {"label": xgb_label, "nodeType": "xgboost",
                          "params": {"modelFile": {"value": model_file, "type": "text"},
                                     "features": {"value": "close", "type": "text"},
                                     "objective": {"value": "binary:logistic", "type": "select"},
                                     "num_class": {"value": 2, "type": "number"}}}},
            ],
            "edges": [
                {"id": "e1->2", "source": "1", "target": "2",
                 "sourceHandle": "1-close", "targetHandle": "2", "type": "default"},
                {"id": "e2->3", "source": "2", "target": "3",
                 "sourceHandle": "2", "targetHandle": "3", "type": "default"},
            ],
        }

    def _model_bytes(self):
        return json.dumps({
            "model_type": "xgboost", "objective": "binary:logistic",
            "num_class": 2, "version": 1, "trees": [], "learner_model_param": {},
        }).encode("utf-8")

    def _meta_bytes(self):
        return json.dumps({
            "strategy_id": "xgb_test", "created_at": "2026-08-06T15:30:12",
            "label": {"type": "classification", "period": 5, "threshold": 0.0},
            "objective": "binary:logistic", "num_class": 2,
            "params": {"learning_rate": 0.1, "max_depth": 3}, "features": ["close"],
        }).encode("utf-8")

    def _deploy_multipart(self, auth_token, name, xgb_label="XGBTestNode",
                          model_file=None, include_model_meta=True):
        strategy = self._xgb_strategy(name, xgb_label, model_file)
        files = {
            "script": ("script.json", json.dumps(strategy).encode("utf-8"), "application/json"),
            f"model_{xgb_label}": (f"{xgb_label}.json", self._model_bytes(), "application/json"),
        }
        if include_model_meta:
            files[f"model_{xgb_label}_meta"] = (
                f"{xgb_label}.meta.json", self._meta_bytes(), "application/json"
            )
        headers = {"Authorization": auth_token} if auth_token else {}
        return requests.post(
            f"{BASE_URL}/strategy", files=files, data={"name": name},
            headers=headers, verify=VERIFY_SSL, timeout=60,
        )

    def test_multipart_deploy_succeeds(self, auth_token):
        resp = self._deploy_multipart(auth_token, self.DEPLOY_NAME)
        assert resp.status_code == 200, f"deploy failed: {resp.status_code} {resp.text}"
        assert resp.json()["name"] == self.DEPLOY_NAME
        TestStrategy().cleanup_strategy(auth_token, self.DEPLOY_NAME)

    def test_cross_strategy_model_file_rejected(self, auth_token):
        resp = self._deploy_multipart(
            auth_token, self.DEPLOY_NAME, "XGBTestNode",
            model_file="production/other_strategy-xgb.json",
        )
        assert resp.status_code == 400, f"应拒绝跨策略引用，实际 {resp.status_code}: {resp.text}"
        body = resp.json()
        assert "不匹配" in body.get("message", "") or "cross" in body.get("message", "").lower()

    def test_plain_json_deploy_still_works(self, auth_token):
        plain_name = self.DEPLOY_NAME + "_plain"
        strategy = self._xgb_strategy(plain_name, "XGBTestNode", model_file="")
        headers = {"Authorization": auth_token} if auth_token else {}
        kwargs = {"verify": False, "headers": headers, "json":
                  {"mode": 0, "name": plain_name, "script": strategy}}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=60)
        assert resp.status_code == 200, f"plain deploy failed: {resp.status_code} {resp.text}"
        TestStrategy().cleanup_strategy(auth_token, plain_name)


# ============== XGBoost 业务层测试 ==============

class TestXGBoostStrategyBusiness:
    """XGBoost 策略业务层测试（区别于 test_xgboost.py 的节点层 E2E）

    目标：验证 multipart deploy + 回测 + 业务结果（metrics）的端到端业务链路。
    避免与 test_xgboost.py::TestXGBoostE2E 重复（后者关心 XGBoostNode 推理正确性）。

    模型来源：ai_test_data/generate_xgb_trivial.py 一次性生成的 1-树 binary 模型。
    策略 JSON：ai_test_data/xgb_trivial_strategy.json（input → MA(5) → XGBoost → Signal → Portfolio → Execution + DebugNode）。
    """

    DEPLOY_NAME = "test_xgb_business"
    XGB_LABEL = "XGBoost"
    SYMBOL = "sz.800001"
    # ai_test_data/ 目录
    DATA_DIR = Path(__file__).parent / "ai_test_data"

    @pytest.fixture(scope="class")
    def deployed(self, auth_token):
        """multipart deploy trivial 模型 + 策略，测试结束自动清理"""
        strategy_path = self.DATA_DIR / "xgb_trivial_strategy.json"
        strategy = json.loads(strategy_path.read_text())
        # 同步 deploy name（策略 JSON 中的 modelFile 必须与 deploy name 一致）
        strategy["id"] = self.DEPLOY_NAME
        strategy["name"] = self.DEPLOY_NAME
        model_file = f"production/{self.DEPLOY_NAME}-{self.XGB_LABEL}.json"

        # meta 用 TestStrategyMultipartDeploy 的 helper（格式符合 XGBoostHandler 校验）
        meta_bytes = TestStrategyMultipartDeploy()._meta_bytes()
        # model 用 generate_xgb_trivial.py 生成的真实 1-树模型
        trivial_model = json.loads(
            (self.DATA_DIR / "xgb_trivial_model.json").read_text())

        files = {
            "script": ("script.json", json.dumps(strategy).encode("utf-8"), "application/json"),
            f"model_{self.XGB_LABEL}": (
                f"{self.XGB_LABEL}.json",
                json.dumps(trivial_model).encode("utf-8"),
                "application/json"),
            f"model_{self.XGB_LABEL}_meta": (
                f"{self.XGB_LABEL}.meta.json", meta_bytes, "application/json"),
        }
        headers = {"Authorization": auth_token}
        resp = requests.post(
            f"{BASE_URL}/strategy", files=files, data={"name": self.DEPLOY_NAME},
            headers=headers, verify=VERIFY_SSL, timeout=60,
        )
        assert resp.status_code == 200, f"deploy 失败: {resp.status_code} {resp.text}"

        yield {"strategy": strategy, "model_file": model_file}

        # 清理
        TestStrategy().cleanup_strategy(auth_token, self.DEPLOY_NAME)

    @pytest.mark.timeout(120)
    def test_xgb_strategy_backtest_runs(self, auth_token, deployed):
        """部署 XGBoost 策略 + 跑回测 + summary 字段完整且数值合理

        验证业务层（区别于 test_xgboost.py L1 节点层）：
        - HTTP 200
        - summary 包含 sharp/max_drawdown/total_return
        - sharp 有限（非 NaN/inf）
        - max_drawdown >= 0
        - total_return 有限
        - indicator_count > 0（flow._collections 应至少有 R2/sharp 等指标）
        """
        resp = requests.post(
            f"{BASE_URL}/backtest",
            json={"script": json.dumps(deployed["strategy"]), "validate": False},
            headers={"Authorization": auth_token},
            verify=VERIFY_SSL,
            timeout=120,
        )
        assert resp.status_code == 200, f"回测失败: {resp.status_code} {resp.text}"

        data = resp.json()
        summary = data.get("summary", {})
        assert summary, f"summary 为空或缺失, 响应: {list(data.keys())}"

        # 字段完整性
        required = ["sharp", "max_drawdown", "total_return"]
        for key in required:
            assert key in summary, f"summary 缺少 {key}, 实际: {list(summary.keys())}"

        # 数值有限性
        import math
        for key in ["sharp", "total_return"]:
            v = summary[key]
            assert isinstance(v, (int, float)), f"{key} 不是数值: {v}"
            assert math.isfinite(v), f"{key} 非有限: {v}"

        # max_drawdown 非负
        assert summary["max_drawdown"] >= 0, f"max_drawdown 应非负: {summary['max_drawdown']}"

        # 至少有一个 flow indicator（R2/sharp 等）
        assert summary.get("indicator_count", 0) > 0, \
            f"indicator_count 应 > 0, 实际: {summary.get('indicator_count')}"

    @pytest.mark.timeout(120)
    def test_xgb_strategy_debug_node_has_probs(self, auth_token, deployed):
        """DebugNode CSV 中 XGBoost 输出概率范围 [0, 1]"""
        import pandas as pd

        # 跑回测（触发 DebugNode Done() 写 CSV）
        resp = requests.post(
            f"{BASE_URL}/backtest",
            json={"script": json.dumps(deployed["strategy"]), "validate": False},
            headers={"Authorization": auth_token},
            verify=VERIFY_SSL,
            timeout=120,
        )
        assert resp.status_code == 200, f"回测失败: {resp.text}"

        # DebugNode 输出路径: {database_path}/data/debug/{strategy}/{label}.csv
        csv_path = DEBUG_DIR / self.DEPLOY_NAME / "xgb_debug.csv"
        assert csv_path.exists(), f"Debug CSV 不存在: {csv_path}"

        df = pd.read_csv(csv_path)
        # XGBoostNode 输出：{symbol}.xgb_probs_0
        prob_col = f"{self.SYMBOL}.xgb_probs_0"
        assert prob_col in df.columns, f"CSV 缺少 {prob_col}, 实际列: {list(df.columns)}"

        probs = df[prob_col].astype(float)
        valid = probs.dropna()
        assert len(valid) > 0, f"{prob_col} 全为 NaN，XGBoost 可能未推理"

        # binary:logistic 输出范围 [0, 1]
        assert (valid >= 0).all() and (valid <= 1).all(), \
            f"概率超出 [0, 1]: min={valid.min()}, max={valid.max()}"
