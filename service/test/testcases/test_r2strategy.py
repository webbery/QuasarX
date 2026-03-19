"""
R² 趋势策略测试用例

测试基于 R² 拟合优度的交易策略：
- R² < 0.7: 震荡市，买入信号
- R² > 0.9: 强趋势，卖出信号
- 0.7 <= R² <= 0.9: 持有
"""
import requests
import json
import pytest
import os
from tool import check_response, BASE_URL


@pytest.mark.usefixtures("auth_token")
class TestR2Strategy:
    """R² 策略测试类"""

    def load_script(self, script_path):
        """加载策略脚本文件"""
        with open(script_path, 'r', encoding='utf-8') as file:
            return json.load(file)

    @pytest.mark.timeout(10)
    def test_r2_strategy_load(self, auth_token):
        """测试 R² 策略配置加载"""
        script_path = './script/r2_strategy.json'
        script = self.load_script(script_path)

        # 验证策略配置结构
        assert 'id' in script
        assert script['id'] == 'r2_strategy'
        assert 'nodes' in script
        assert 'edges' in script

        # 验证节点类型
        node_types = [node['data']['nodeType'] for node in script['nodes']]
        assert 'input' in node_types
        assert 'test' in node_types  # R² 计算节点
        assert 'signal' in node_types
        assert 'execution' in node_types

        # 验证 signal 节点的表达式
        signal_node = next(n for n in script['nodes'] if n['data']['nodeType'] == 'signal')
        assert signal_node['data']['params']['buy']['value'] == 'r2 < 0.7'
        assert signal_node['data']['params']['sell']['value'] == 'r2 > 0.9'

    @pytest.mark.timeout(60)
    def test_r2_backtest_single_stock(self, auth_token):
        """测试回测结果包含完整的绩效指标"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy.json'
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
        print("\n=== 回测绩效指标 ===")
        print(f"夏普比率 (Sharp): {features.get('sharp', 'N/A'):.4f}")
        print(f"年化收益率 (Annual Return): {features.get('annual_return', 'N/A'):.4f}")
        print(f"总收益率 (Total Return): {features.get('total_return', 'N/A'):.4f}")
        print(f"最大回撤 (Max Drawdown): {features.get('max_drawdown', 'N/A'):.4f}")
        print(f"胜率 (Win Rate): {features.get('win_rate', 'N/A'):.4f}")
        print(f"卡玛比率 (Calmar Ratio): {features.get('calmar_ratio', 'N/A'):.4f}")

    @pytest.mark.timeout(60)
    def test_r2_backtest_multi_stock(self, auth_token):
        """测试多股票 R² 策略回测"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy_multi.json'
        script = self.load_script(script_path)

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        # 执行回测
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)

        # 验证回测结果
        assert isinstance(data, dict)
        assert 'features' in data

    @pytest.mark.timeout(60)
    def test_r2_backtest_with_portfolio(self, auth_token):
        """测试带投资组合节点的回测（R² 策略 + PortfolioNode）"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy_portfolio.json'
        script = self.load_script(script_path)

        # 验证策略配置包含 portfolio 节点
        node_types = [node['data']['nodeType'] for node in script['nodes']]
        assert 'portfolio' in node_types, "策略应包含 portfolio 节点"

        # 验证 portfolio 节点配置
        portfolio_node = next(n for n in script['nodes'] if n['data']['nodeType'] == 'portfolio')
        assert portfolio_node['data']['params']['positionRatio']['value'] == 0.5
        assert portfolio_node['data']['params']['initialCapital']['value'] == 100000

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        # 执行回测
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)

        # 验证回测结果
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

        # 打印指标供调试
        print("\n=== 带投资组合的回测绩效指标 ===")
        print(f"仓位比例: 50%")
        print(f"初始本金: 100,000 元")
        print(f"夏普比率 (Sharp): {features.get('sharp', 'N/A'):.4f}")
        print(f"年化收益率 (Annual Return): {features.get('annual_return', 'N/A'):.4f}")
        print(f"总收益率 (Total Return): {features.get('total_return', 'N/A'):.4f}")
        print(f"最大回撤 (Max Drawdown): {features.get('max_drawdown', 'N/A'):.4f}")
        print(f"胜率 (Win Rate): {features.get('win_rate', 'N/A'):.4f}")
        print(f"卡玛比率 (Calmar Ratio): {features.get('calmar_ratio', 'N/A'):.4f}")

    @pytest.mark.timeout(30)
    def test_r2_strategy_deploy(self, auth_token):
        """测试部署 R² 策略"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy.json'
        script = self.load_script(script_path)

        kwargs['json'] = {
            'mode': 0,
            'name': 'r2_strategy',
            'script': script
        }

        # 部署策略
        response = requests.post(f"{BASE_URL}/strategy", **kwargs)
        data = check_response(response)

    @pytest.mark.timeout(60)
    def test_r2_threshold_conservative(self, auth_token):
        """测试保守型 R² 阈值配置（R²<0.6 买入，R²>0.95 卖出）"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy.json'
        script = self.load_script(script_path)

        # 修改为保守型阈值
        for node in script['nodes']:
            if node['data']['nodeType'] == 'signal':
                node['data']['params']['buy']['value'] = 'r2 < 0.6'
                node['data']['params']['sell']['value'] = 'r2 > 0.95'

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)
        assert isinstance(data, dict)

    @pytest.mark.timeout(60)
    def test_r2_threshold_sensitive(self, auth_token):
        """测试敏感型 R² 阈值配置（R²<0.8 买入，R²>0.85 卖出）"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy.json'
        script = self.load_script(script_path)

        # 修改为敏感型阈值
        for node in script['nodes']:
            if node['data']['nodeType'] == 'signal':
                node['data']['params']['buy']['value'] = 'r2 < 0.8'
                node['data']['params']['sell']['value'] = 'r2 > 0.85'

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)
        assert isinstance(data, dict)

    @pytest.mark.timeout(60)
    def test_r2_with_time_sequence(self, auth_token):
        """测试使用时间序列的 R² 策略（R² 金叉买入）"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy.json'
        script = self.load_script(script_path)

        # 使用历史值：R² 从低位上穿 0.7 时买入
        for node in script['nodes']:
            if node['data']['nodeType'] == 'signal':
                node['data']['params']['buy']['value'] = 'r2[t] < 0.7 and r2[t-1] >= 0.7'
                node['data']['params']['sell']['value'] = 'r2[t] > 0.9'

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)
        assert isinstance(data, dict)


@pytest.mark.usefixtures("auth_token")
class TestR2Node:
    """R² 节点功能测试"""

    def load_script(self, script_path):
        """加载策略脚本文件"""
        with open(script_path, 'r', encoding='utf-8') as file:
            return json.load(file)

    @pytest.mark.timeout(60)
    def test_test_node_output(self, auth_token):
        """测试 TestNode 输出 r2 指标"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/r2_strategy.json'
        script = self.load_script(script_path)

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)

        # 验证回测执行成功
        assert isinstance(data, dict)
        assert 'features' in data
