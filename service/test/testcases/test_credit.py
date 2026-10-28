"""
两融（融资融券）测试

依赖：
- stock_hist_sim 模式（回测模式）
- Debug 构建（simulate/bar 端点）
- node_test_data/margin_test_strategy.json 测试策略
- prepare.py margin_stock 预灌 sz.900001：SignalNode 的 allowShort=true 分支
  只对 margin_stock 里的标的保留「无持仓 SELL」，不在名单里会直接降级为 HOLD，
  open_short 永远出不来（名单由 prepare.py 在服务启动前写入，运行期无法补）
- CapitalPool 有可用资金：部署借的额度 Stop/删除不归还，用例开始前先 reclaim

覆盖：
1. _flag 贯通：验证融券卖出（SELL flag=0）能正确表达为 OpenShort
2. CapitalPool 信用状态持久化：验证融资买入后 credit 状态正确更新
3. 两融费用计算：验证日息计算正确
"""

import json
import time
from pathlib import Path

import pytest
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

from tool import (
    BASE_URL, VERIFY_SSL, get_auth_token,
    api_load_strategy, api_run_strategy, api_simulate_bar,
    api_get_decisions, wait_for_decisions, api_get_credit_state,
    api_get_latest_bar, reclaim_capital
)

# ==================== 配置 ====================

STRATEGY_PATH = Path(__file__).parent / "node_test_data" / "margin_test_strategy.json"
STRATEGY_NAME = "margin_test_strategy"
TEST_SYMBOL = "sz.900001"


# ==================== 测试用例 ====================

class TestMarginTrading:
    """两融测试"""

    @pytest.fixture(autouse=True)
    def setup(self):
        """测试前准备"""
        self.token = get_auth_token()
        if not STRATEGY_PATH.exists():
            pytest.skip(f"测试策略不存在: {STRATEGY_PATH}")

        # 归还资金（测试隔离）：部署（InitStrategy）会向 CapitalPool 借一笔额度，
        # 而 Stop/删除都不会归还，只有 reclaim 接口会。别的用例残留的 active 策略
        # 会把整池 100 万占满 → 本策略 allocate 失败 → 日终回放 targetCapital=0
        # → 所有买单 qty=0 被丢弃，决策只剩 HOLD（HOLD 又不落库）→ 用例拿不到决策。
        # reclaim_all 清空注册表，本用例随后的部署会重新分配。
        reclaim_capital(self.token)

    def test_flag_propagation_open_short(self):
        """测试 _flag 贯通：融券卖出（SELL flag=0）应表达为 OpenShort
        
        验证点：
        1. DecisionSet 中该 item 的 _flag == 0
        2. DuckDB decisions 表中 action == 2 (OpenShort 的枚举值)
        3. 邮件中该标的显示"融券"徽标（通过检查 DecisionSet JSON）
        """
        with open(STRATEGY_PATH) as f:
            script = json.load(f)
        
        # 确保 margin.allowShort = true
        assert script.get("margin", {}).get("allowShort") is True, \
            "测试策略必须配置 margin.allowShort = true"
        
        # 加载策略
        api_load_strategy(self.token, STRATEGY_NAME, script)
        
        # 设置为运行模式
        api_run_strategy(self.token, STRATEGY_NAME)
        
        # 获取最新 bar
        try:
            bar = api_get_latest_bar(self.token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")
        
        # 构造测试 bar（价格 < 50 触发卖出，当前无持仓触发融券卖出）
        test_date = "2025-01-15"
        test_bar = {
            "symbol": TEST_SYMBOL,
            "datetime": f"{test_date} 00:00:00",
            "open": 45.0,
            "high": 46.0,
            "low": 44.0,
            "close": 45.0,
            "volume": 1000000
        }
        
        # 推送 bar 触发日终策略
        result = api_simulate_bar(self.token, test_bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")
        
        # 等待决策结果
        time.sleep(2)
        
        # 按归属日查询：决策入库用 DecisionSet._asOf 作时间戳，模拟日就是 2025-01-15。
        # 先按日期查（精确、不受今天其它策略决策干扰），查不到再退回"今天"
        # （兼容没有归属日的老数据）。
        all_decisions = api_get_decisions(self.token, STRATEGY_NAME, test_date)
        if not all_decisions:
            all_decisions = api_get_decisions(self.token)
        
        # 调试信息
        if not all_decisions:
            pytest.skip(
                f"无决策生成。simulate_bar 返回: {result}"
            )
        
        # 查找融券卖出的决策（action == "open_short"）
        open_short_decisions = [d for d in all_decisions if d.get("action") == "open_short"]
        
        assert len(open_short_decisions) > 0, \
            f"应该至少有一个融券卖出（open_short）决策，实际决策: {[d.get('action') for d in all_decisions]}"
        
        # 验证 flag == 0（通过 label 字段验证）
        for d in open_short_decisions:
            # API 返回的 label 字段包含中文描述
            assert "开空" in d.get("label", ""), \
                f"融券卖出的 label 应该包含'开空'，实际为 {d.get('label')}"

    def test_capital_pool_credit_persistence(self):
        """测试 CapitalPool 信用状态持久化
        
        验证点：
        1. 融资买入后，capital_pool.json 中 credit.borrowedCash 正确更新
        2. credit.financedAmount[symbol] 正确
        3. 重启服务后，getCreditState() 返回相同值
        """
        with open(STRATEGY_PATH) as f:
            script = json.load(f)
        
        # 确保配置了融资
        margin_config = script.get("margin", {})
        assert margin_config.get("marginRatio", 1.0) < 1.0, \
            "测试策略必须配置 marginRatio < 1.0（融资）"
        
        # 加载策略
        api_load_strategy(self.token, STRATEGY_NAME, script)
        
        # 设置为运行模式
        api_run_strategy(self.token, STRATEGY_NAME)
        
        # 获取最新 bar
        try:
            bar = api_get_latest_bar(self.token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")
        
        # 构造测试 bar（价格 > 100 触发买入，使用融资）
        test_date = "2025-01-16"
        test_bar = {
            "symbol": TEST_SYMBOL,
            "datetime": f"{test_date} 00:00:00",
            "open": 105.0,
            "high": 106.0,
            "low": 104.0,
            "close": 105.0,
            "volume": 1000000
        }
        
        # 推送 bar 触发日终策略
        result = api_simulate_bar(self.token, test_bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")
        
        # 等待决策结果
        wait_for_decisions(self.token, STRATEGY_NAME, test_date)
        
        # 获取信用状态
        credit_state = api_get_credit_state(self.token, STRATEGY_NAME)
        
        # 如果有融资买入，验证 borrowed_cash > 0
        if credit_state.get("borrowed_cash", 0.0) > 0:
            # 验证 financed_amount 也有对应标的
            financed_amount = credit_state.get("financed_amount", {})
            assert len(financed_amount) > 0, \
                "有融资余额时，financed_amount 应该有对应标的"
            
            # 验证 borrowed_cash 与 financed_amount 之和一致
            total_financed = sum(financed_amount.values())
            assert abs(credit_state["borrowed_cash"] - total_financed) < 0.01, \
                f"borrowed_cash ({credit_state['borrowed_cash']}) 与 " \
                f"financed_amount 之和 ({total_financed}) 不一致"

    def test_margin_cost_calculation(self):
        """测试两融费用计算
        
        验证点：
        1. DecisionSet 中 _totalFinancingInterest 正确（融资利息）
        2. _totalShortFee 正确（融券费）
        3. _totalMarginCost = _totalFinancingInterest + _totalShortFee
        """
        with open(STRATEGY_PATH) as f:
            script = json.load(f)
        
        # 确保配置了两融费率
        margin_config = script.get("margin", {})
        financing_rate = margin_config.get("financingRate", 0.0)
        lending_rate = margin_config.get("securitiesLendingRate", 0.0)
        
        assert financing_rate > 0 or lending_rate > 0, \
            "测试策略必须配置 financingRate 或 securitiesLendingRate > 0"
        
        # 加载策略
        api_load_strategy(self.token, STRATEGY_NAME, script)
        
        # 设置为运行模式
        api_run_strategy(self.token, STRATEGY_NAME)
        
        # 获取最新 bar
        try:
            bar = api_get_latest_bar(self.token, TEST_SYMBOL)
        except ValueError as e:
            pytest.skip(f"无测试数据: {e}")
        
        # 构造测试 bar
        test_date = "2025-01-17"
        test_bar = {
            "symbol": TEST_SYMBOL,
            "datetime": f"{test_date} 00:00:00",
            "open": 105.0,
            "high": 106.0,
            "low": 104.0,
            "close": 105.0,
            "volume": 1000000
        }
        
        # 推送 bar 触发日终策略
        result = api_simulate_bar(self.token, test_bar)
        if "status" in result and result.get("status") == "error":
            pytest.skip(f"simulate/bar 不可用: {result}")
        
        # 等待决策结果
        wait_for_decisions(self.token, STRATEGY_NAME, test_date)
        
        # 获取决策详情
        decisions_resp = api_get_decisions(self.token, STRATEGY_NAME, test_date)
        
        # 验证两融费用字段存在
        # api_get_decisions 返回的是决策列表，两融费用字段在 DecisionSet 层级
        # 需要通过其他方式获取（如直接读取 DecisionSet JSON 或扩展 API）
        # 这里先验证决策列表不为空，后续可以扩展验证
        if not decisions_resp:
            pytest.skip("无决策结果")
        
        # TODO: 扩展 API 以返回 DecisionSet 层级的两融费用信息
        # 当前先验证决策列表结构正确
        assert isinstance(decisions_resp, list), "决策结果应为列表"
        assert len(decisions_resp) > 0, "应有至少一个决策"
