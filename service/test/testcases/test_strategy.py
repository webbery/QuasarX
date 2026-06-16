import requests
import sys
from tool import check_response, BASE_URL
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
        script_dir = './script'
        for item_name in os.listdir(script_dir):
            if item_name == 'ml.json':
                continue

            item_path = os.path.join(script_dir, item_name)
            print('run script:', item_name)
            script = self.load_script(item_path)
            if len(script) < 2:
                return

            kwargs['json'] = {
                "script": json.dumps(script, ensure_ascii=False)
            }
            response = requests.post(f"{BASE_URL}/backtest", **kwargs)
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
