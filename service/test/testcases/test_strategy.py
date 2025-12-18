import requests
import sys
from tool import check_response, BASE_URL
import pytest
import json
import os

@pytest.mark.usefixtures("auth_token")
class TestStrategy:
    @pytest.mark.timeout(5)
    def test_strategy(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}
        response = requests.get(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)
        assert isinstance(data, list)
        assert len(data) > 0

    @pytest.mark.timeout(5)
    def test_upload_model(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        with open("../models/lstm_model.onnx", "rb") as f:
            files = {"file": ("lstm_model.onnx", f)}
            response = requests.put(
                f"{BASE_URL}/strategy/node",
                files=files,
                **kwargs
            )
        check_response(response)
        
    def load_script(self, script_path):
        # 使用 with open 语句确保文件正确关闭
        with open(script_path, 'r', encoding='utf-8') as file:
            return json.load(file)  # data 现在是 Python 字典或列表

    @pytest.mark.timeout(600)
    def test_run_all_script(self, auth_token):
        kwargs = {
            'verify': False  # 始终禁用 SSL 验证
        }
        if auth_token and len(auth_token) > 10:  # 确保 token 非空且长度有效
            kwargs['headers'] = {'Authorization': auth_token}

        no_reply = ['ml.json']
        script_dir = './script'
        for item_name in os.listdir(script_dir):
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

    