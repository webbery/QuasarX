'''
双均线策略：
- 金叉买入：短期均线上穿长期均线
- 死叉卖出：短期均线下穿长期均线
'''
import requests
import json
import pytest
import os
from tool import check_response, BASE_URL

@pytest.mark.usefixtures("auth_token")
class TestMa2Strategy:
    """双均线策略测试类"""

    def load_script(self, script_path):
        """加载策略脚本文件"""
        with open(script_path, 'r', encoding='utf-8') as file:
            return json.load(file)

    @pytest.mark.timeout(10)
    def test_ma2_strategy_load(self, auth_token):
        """测试双均线策略配置加载"""
        script_path = './script/ma2_strategy.json'
        script = self.load_script(script_path)

        # 验证策略配置结构
        assert 'id' in script
        assert script['id'] == 'ma2_strategy'
        assert 'nodes' in script
        assert 'edges' in script

        # 验证节点类型
        node_types = [node['data']['nodeType'] for node in script['nodes']]
        assert 'input' in node_types
        assert 'test' in node_types  # 双均线计算节点
        assert 'signal' in node_types
        assert 'execution' in node_types

        # 验证 test 节点配置
        test_node = next(n for n in script['nodes'] if n['data']['nodeType'] == 'test')
        assert test_node['data']['params']['shortPeriod']['value'] == 5
        assert test_node['data']['params']['longPeriod']['value'] == 15

        # 验证 signal 节点的表达式
        signal_node = next(n for n in script['nodes'] if n['data']['nodeType'] == 'signal')
        assert 'ma_short' in signal_node['data']['params']['buy']['value']
        assert 'ma_long' in signal_node['data']['params']['buy']['value']

    @pytest.mark.timeout(60)
    def test_ma2_backtest_single_stock(self, auth_token):
        """测试双均线策略回测结果包含完整的绩效指标"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/ma2_strategy.json'
        script = self.load_script(script_path)

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        # 执行回测
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)

        # 验证回测结果包含所有绩效指标
        assert isinstance(data, dict)
        assert 'features' in data, "回测结果应包含统计指标"

        features = data['features']

        # 验证关键指标存在
        expected_metrics = [
            'sharp',           # 夏普比率
            'annual_return',   # 年化收益率
            'total_return',    # 总收益率
            'max_drawdown',    # 最大回撤
            'win_rate',        # 胜率
            'calmar_ratio'     # 卡玛比率
        ]

        for metric in expected_metrics:
            assert metric in features, f"回测结果应包含 {metric} 指标"

        # 验证指标值为数值类型
        for metric in expected_metrics:
            assert isinstance(features[metric], (int, float)), f"{metric} 应为数值类型"

        # 打印指标供调试
        print("\n=== 双均线策略回测绩效指标 ===")
        print(f"短期均线周期: 5 日")
        print(f"长期均线周期: 15 日")
        print(f"夏普比率 (Sharp): {features.get('sharp', 'N/A'):.4f}")
        print(f"年化收益率 (Annual Return): {features.get('annual_return', 'N/A'):.4f}")
        print(f"总收益率 (Total Return): {features.get('total_return', 'N/A'):.4f}")
        print(f"最大回撤 (Max Drawdown): {features.get('max_drawdown', 'N/A'):.4f}")
        print(f"胜率 (Win Rate): {features.get('win_rate', 'N/A'):.4f}")
        print(f"卡玛比率 (Calmar Ratio): {features.get('calmar_ratio', 'N/A'):.4f}")

    @pytest.mark.timeout(30)
    def test_ma2_strategy_deploy(self, auth_token):
        """测试部署双均线策略"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/ma2_strategy.json'
        script = self.load_script(script_path)

        kwargs['json'] = {
            'mode': 0,
            'name': 'ma2_strategy',
            'script': script
        }

        # 部署策略
        response = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)

    @pytest.mark.timeout(60)
    def test_ma2_with_different_periods(self, auth_token):
        """测试不同均线周期的策略"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)

        # 修改为 10/30 周期
        for node in script['nodes']:
            if node['data']['nodeType'] == 'function' and node['data']['label'] == 'ma_short':
                node['data']['params']['smoothTime']['value'] = 10
            if node['data']['nodeType'] == 'function' and node['data']['label'] == 'ma_long':
                node['data']['params']['smoothTime']['value'] = 30

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)
        assert isinstance(data, dict)
        assert 'features' in data

    @pytest.mark.timeout(60)
    def test_ma2_backtest_graph_stock(self, auth_token):
        """测试双均线策略回测结果包含完整的绩效指标"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/ma_graph_strategy.json'
        script = self.load_script(script_path)

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        # 执行回测
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)

        # 验证回测结果包含所有绩效指标
        assert isinstance(data, dict)
        assert 'features' in data, "回测结果应包含统计指标"

        features = data['features']

        # 验证关键指标存在
        expected_metrics = [
            'sharp',           # 夏普比率
            'annual_return',   # 年化收益率
            'total_return',    # 总收益率
            'max_drawdown',    # 最大回撤
            'win_rate',        # 胜率
            'calmar_ratio'     # 卡玛比率
        ]

        for metric in expected_metrics:
            assert metric in features, f"回测结果应包含 {metric} 指标"

        # 验证指标值为数值类型
        for metric in expected_metrics:
            assert isinstance(features[metric], (int, float)), f"{metric} 应为数值类型"

        # 打印指标供调试
        print("\n=== 双均线策略回测绩效指标 ===")
        print(f"短期均线周期: 5 日")
        print(f"长期均线周期: 15 日")
        print(f"夏普比率 (Sharp): {features.get('sharp', 'N/A'):.4f}")
        print(f"年化收益率 (Annual Return): {features.get('annual_return', 'N/A'):.4f}")
        print(f"总收益率 (Total Return): {features.get('total_return', 'N/A'):.4f}")
        print(f"最大回撤 (Max Drawdown): {features.get('max_drawdown', 'N/A'):.4f}")
        print(f"胜率 (Win Rate): {features.get('win_rate', 'N/A'):.4f}")
        print(f"卡玛比率 (Calmar Ratio): {features.get('calmar_ratio', 'N/A'):.4f}")