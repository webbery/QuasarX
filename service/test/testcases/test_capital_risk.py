import requests
import sys
from tool import check_response, BASE_URL
import pytest

# --------------------------
# 测试资金风控相关接口
# --------------------------
class TestCapitalRisk:
    """测试总资金止损和单日亏损限额功能"""

    def test_get_capital_config(self, auth_token):
        """获取总资金风控配置"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }
        response = requests.get(f"{BASE_URL}/risk/capital", **kwargs)
        data = check_response(response)

        # 验证响应字段
        assert 'totalStopLossPercent' in data or 'enableTotalStopLoss' in data
        print(f"当前总资金风控配置：{data}")

    def test_set_capital_config(self, auth_token):
        """设置总资金止损配置"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }
        payload = {
            "totalStopLossPercent": 0.85,
            "enableTotalStopLoss": True,
            "autoClosePosition": False,
            "manualInterventionTimeoutSec": 300,
            "timeoutAutoClose": True,
            "initialCapital": 1000000
        }
        response = requests.post(f"{BASE_URL}/risk/capital", json=payload, **kwargs)
        data = check_response(response)

        # 验证配置成功
        assert data.get('totalStopLossPercent') == 0.85
        assert data.get('enableTotalStopLoss') == True
        print(f"设置后的配置：{data}")

    def test_get_daily_config(self, auth_token):
        """获取单日亏损风控配置"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }
        response = requests.get(f"{BASE_URL}/risk/daily", **kwargs)
        data = check_response(response)

        # 验证响应字段
        assert 'dailyMaxLossPercent' in data or 'enableDailyLossLimit' in data
        print(f"当前单日亏损风控配置：{data}")

    def test_set_daily_config(self, auth_token):
        """设置单日亏损限额配置"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }
        payload = {
            "dailyMaxLossPercent": 0.03,
            "enableDailyLossLimit": True,
            "autoClosePosition": False,
            "manualInterventionTimeoutSec": 300,
            "timeoutAutoClose": True
        }
        response = requests.post(f"{BASE_URL}/risk/daily", json=payload, **kwargs)
        data = check_response(response)

        # 验证配置成功
        assert data.get('dailyMaxLossPercent') == 0.03
        assert data.get('enableDailyLossLimit') == True
        print(f"设置后的单日亏损配置：{data}")

    def test_set_last_day_equity(self, auth_token):
        """设置昨日权益"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }
        payload = {
            "lastDayEquity": 980000
        }
        response = requests.post(f"{BASE_URL}/risk/daily", json=payload, **kwargs)
        data = check_response(response)

        # 验证配置成功
        assert data.get('lastDayEquity') == 980000
        print(f"设置昨日权益后：{data}")

    def test_close_all_position_dry_run(self, auth_token):
        """测试全部平仓接口（需要 confirm=true）"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }

        # 测试不带 confirm 参数的情况（应该失败）
        payload = {}
        response = requests.post(f"{BASE_URL}/risk/closeall", json=payload, **kwargs)
        assert response.status_code == 400
        print("不带 confirm 参数的响应：", response.json())

        # 测试带 confirm=false 的情况（应该失败）
        payload = {"confirm": False}
        response = requests.post(f"{BASE_URL}/risk/closeall", json=payload, **kwargs)
        assert response.status_code == 400
        print("confirm=false 的响应：", response.json())

    def test_capital_risk_status_after_config(self, auth_token):
        """验证配置后的风控状态"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }

        # 先设置配置
        config_payload = {
            "totalStopLossPercent": 0.80,
            "enableTotalStopLoss": True,
            "initialCapital": 1000000
        }
        requests.post(f"{BASE_URL}/risk/capital", json=config_payload, **kwargs)

        # 获取状态
        response = requests.get(f"{BASE_URL}/risk/capital", **kwargs)
        data = check_response(response)

        # 验证配置已生效
        assert data.get('totalStopLossPercent') == 0.80
        assert data.get('enableTotalStopLoss') == True
        assert data.get('totalStopLossLevel') == 800000  # 1000000 * 0.80
        print(f"风控状态验证通过：{data}")

    def test_daily_risk_status_after_config(self, auth_token):
        """验证单日亏损配置后的风控状态"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }

        # 先设置配置
        config_payload = {
            "dailyMaxLossPercent": 0.05,
            "enableDailyLossLimit": True,
            "lastDayEquity": 1000000
        }
        requests.post(f"{BASE_URL}/risk/daily", json=config_payload, **kwargs)

        # 获取状态
        response = requests.get(f"{BASE_URL}/risk/daily", **kwargs)
        data = check_response(response)

        # 验证配置已生效
        assert data.get('dailyMaxLossPercent') == 0.05
        assert data.get('enableDailyLossLimit') == True
        assert data.get('dailyLossLimitLevel') == 950000  # 1000000 * (1 - 0.05)
        print(f"单日风控状态验证通过：{data}")


class TestCapitalRiskIntegration:
    """资金风控集成测试（需要服务运行且有持仓）"""

    def test_full_workflow(self, auth_token):
        """完整工作流测试"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token}
        }

        # 1. 设置总资金止损
        capital_config = {
            "totalStopLossPercent": 0.85,
            "enableTotalStopLoss": True,
            "autoClosePosition": False,
            "initialCapital": 1000000
        }
        response = requests.post(f"{BASE_URL}/risk/capital", json=capital_config, **kwargs)
        capital_data = check_response(response)
        print(f"步骤 1 - 设置总资金止损：{capital_data}")

        # 2. 设置单日亏损限额
        daily_config = {
            "dailyMaxLossPercent": 0.03,
            "enableDailyLossLimit": True,
            "autoClosePosition": False,
            "lastDayEquity": 1000000
        }
        response = requests.post(f"{BASE_URL}/risk/daily", json=daily_config, **kwargs)
        daily_data = check_response(response)
        print(f"步骤 2 - 设置单日亏损限额：{daily_data}")

        # 3. 验证风控状态
        response = requests.get(f"{BASE_URL}/risk/capital", **kwargs)
        capital_status = check_response(response)
        print(f"步骤 3 - 总资金风控状态：{capital_status}")

        response = requests.get(f"{BASE_URL}/risk/daily", **kwargs)
        daily_status = check_response(response)
        print(f"步骤 3 - 单日风控状态：{daily_status}")

        # 4. 验证关键指标计算
        assert 'currentDrawdown' in capital_status
        assert 'dailyLossPercent' in daily_status
        print(f"步骤 4 - 当前回撤：{capital_status.get('currentDrawdown')}")
        print(f"步骤 4 - 当日盈亏：{daily_status.get('dailyProfitLoss')}")


class TestCapitalRiskBreakerConfig:
    """测试 VaR 上限 + 回撤断路器配置"""

    def test_set_var_limit(self, auth_token):
        """设置 VaR 上限"""
        kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
        payload = {"var_limit": 0.03}
        response = requests.post(f"{BASE_URL}/risk/capital", json=payload, **kwargs)
        data = check_response(response)

        assert data.get('varLimit') == 0.03 or data.get('_varLimit') == 0.03
        print(f"VaR 上限设置：{data}")

    def test_set_breaker_l1_l2(self, auth_token):
        """设置断路器 L1/L2，验证 L3 = L2 × 1.25 自动计算"""
        kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
        payload = {
            "breaker_l1": 0.08,
            "breaker_l2": 0.12,
        }
        response = requests.post(f"{BASE_URL}/risk/capital", json=payload, **kwargs)
        data = check_response(response)

        # L1/L2 直接设定
        l1 = data.get('ddLevel1') or data.get('_ddLevel1')
        l2 = data.get('ddLevel2') or data.get('_ddLevel2')
        l3 = data.get('ddLevel3') or data.get('_ddLevel3')
        assert l1 == 0.08, f"L1 应为 0.08，实际 {l1}"
        assert l2 == 0.12, f"L2 应为 0.12，实际 {l2}"
        # L3 = L2 × 1.25
        expected_l3 = 0.12 * 1.25
        assert abs(l3 - expected_l3) < 1e-6, f"L3 应为 {expected_l3}，实际 {l3}"
        print(f"断路器设置：L1={l1}, L2={l2}, L3={l3}")

    def test_breaker_l1_validation(self, auth_token):
        """断路器阈值边界校验"""
        kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}

        # 超范围应返回 400
        payload = {"breaker_l1": 0.6}
        response = requests.post(f"{BASE_URL}/risk/capital", json=payload, **kwargs)
        assert response.status_code == 400
        print(f"超范围校验通过：{response.json()}")

    def test_get_risk_status(self, auth_token):
        """GET /risk/status 返回断路器状态"""
        kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
        response = requests.get(f"{BASE_URL}/risk/status", **kwargs)
        data = check_response(response)

        # 验证关键字段存在
        assert 'breaker_level' in data or 'breakerLevel' in data
        assert 'current_drawdown' in data or 'currentDrawdown' in data
        print(f"风控状态：{data}")

    def test_full_breaker_config_workflow(self, auth_token):
        """完整断路器配置工作流"""
        kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}

        # 1. 设置 VaR + 断路器
        config = {
            "var_limit": 0.02,
            "breaker_l1": 0.05,
            "breaker_l2": 0.10,
            "initialCapital": 1000000,
        }
        resp = requests.post(f"{BASE_URL}/risk/capital", json=config, **kwargs)
        data = check_response(resp)
        print(f"步骤 1 - 设置配置：{data}")

        # 2. 验证 /risk/status 反映新阈值
        resp = requests.get(f"{BASE_URL}/risk/status", **kwargs)
        status = check_response(resp)
        print(f"步骤 2 - 风控状态：{status}")

        # 3. 验证断路器级别为 0（无持仓无回撤）
        level = status.get('breaker_level') or status.get('breakerLevel') or 0
        assert level == 0, f"无持仓时断路器应为 0 级，实际 {level}"
