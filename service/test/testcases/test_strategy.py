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
    def test_strategy(self, auth_token):
        """测试获取策略列表，验证新返回格式 [{name, running}, ...]"""
        kwargs = self._auth_kwargs(auth_token)
        response = requests.get(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)

        assert isinstance(data, list)
        if len(data) > 0:
            # 验证返回格式：每项必须有 name 和 running 字段
            item = data[0]
            assert 'name' in item, "策略列表项缺少 'name' 字段"
            assert 'running' in item, "策略列表项缺少 'running' 字段"
            assert isinstance(item['running'], bool), "'running' 字段应为布尔值"

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

    @pytest.mark.timeout(30)
    def test_deploy(self, auth_token):
        """部署并运行策略，验证返回格式 + 策略列表状态"""
        kwargs = self._auth_kwargs(auth_token)

        script_path = './script/ma_graph_strategy.json'
        if not os.path.exists(script_path):
            pytest.skip(f"策略脚本不存在: {script_path}")

        script = self.load_script(script_path)
        kwargs['json'] = {
            'mode': 0,
            'name': self.STRATEGY_NAME,
            'script': script
        }
        response = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)

        assert data['message'] == 'success', f"部署失败: {data}"
        assert data['name'] == self.STRATEGY_NAME
        assert data['running'] is True, "部署后策略应处于运行状态"

        # 验证策略列表中可查找到
        strategies = self.get_all_strategies(auth_token)
        found = self.find_strategy(strategies, self.STRATEGY_NAME)
        assert found is not None, "部署的策略应在策略列表中"
        assert found['running'] is True

    @pytest.mark.timeout(30)
    def test_delete_strategy(self, auth_token):
        """删除策略，验证从列表中消失"""
        # 先部署一个用于删除的策略
        cleanup_name = 'test_delete_target'
        self.cleanup_strategy(auth_token, cleanup_name)

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)
        kwargs = self._auth_kwargs(auth_token)
        kwargs['json'] = {'mode': 0, 'name': cleanup_name, 'script': script}
        response = requests.post(f"{BASE_URL}/strategy", **kwargs)
        check_response(response)

        # 删除策略
        kwargs['json'] = {'name': cleanup_name}
        response = requests.delete(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)
        assert data['message'] == 'success'

        # 验证策略列表中不再存在
        strategies = self.get_all_strategies(auth_token)
        found = self.find_strategy(strategies, cleanup_name)
        assert found is None, "删除的策略不应在策略列表中"

    @pytest.mark.timeout(60)
    def test_strategy_lifecycle(self, auth_token):
        """完整生命周期测试：部署 → 停止 → 再运行 → 再停止 → 删除"""
        lifecycle_name = 'test_lifecycle'
        self.cleanup_strategy(auth_token, lifecycle_name)

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)
        kwargs = self._auth_kwargs(auth_token)

        # 1. 部署
        kwargs['json'] = {'mode': 0, 'name': lifecycle_name, 'script': script}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is True

        # 2. 停止
        kwargs['json'] = {'mode': 2, 'name': lifecycle_name}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is False

        # 3. 再运行
        kwargs['json'] = {'mode': 1, 'name': lifecycle_name}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is True

        # 4. 再停止
        kwargs['json'] = {'mode': 2, 'name': lifecycle_name}
        resp = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['running'] is False

        # 5. 删除
        kwargs['json'] = {'name': lifecycle_name}
        resp = requests.delete(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(resp)
        assert data['message'] == 'success'

        strategies = self.get_all_strategies(auth_token)
        assert self.find_strategy(strategies, lifecycle_name) is None

    @pytest.mark.timeout(30)
    def test_redeploy_overwrite(self, auth_token):
        """重复部署同名策略，应覆盖成功"""
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
    def test_stop_nonexistent_strategy(self, auth_token):
        """停止不存在的策略，服务不应崩溃"""
        kwargs = self._auth_kwargs(auth_token)
        kwargs['json'] = {'mode': 2, 'name': 'nonexistent_strategy_12345'}

        try:
            response = requests.post(f"{BASE_URL}/strategy", **kwargs, timeout=5)
            # 服务不崩溃即可，状态码可以是 400/404/500
            assert response.status_code in (200, 400, 404, 500)
        except requests.exceptions.RequestException:
            pytest.fail("停止不存在策略时服务无响应")

    @pytest.mark.timeout(600)
    def test_run_all_script(self, auth_token):
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
