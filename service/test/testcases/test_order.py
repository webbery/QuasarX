import requests
import sys
from tool import check_response, BASE_URL
import pytest
import math
import json
import time
import os
from typing import Optional, Dict, Any, List
from pathlib import Path

# ==================== 配置管理 ====================
class TestConfig:
    """测试配置管理"""

    DEFAULT_CONFIG = {
        "base_url": "https://localhost:19107/v0",
        "default_test_stock": "600000",
        "test_stocks": ["600000", "600036", "000001"],
        "default_quantity": 100,
        "order_status_timeout": 30,
        "order_poll_interval": 1.0,
        "skip_cleanup": False,
        "test_scenarios": []
    }

    def __init__(self):
        self._config = self.DEFAULT_CONFIG.copy()
        self._load_config()

    def _load_config(self):
        """从配置文件加载配置"""
        config_paths = [
            Path(__file__).parent / "test_config.json",
            Path(__file__).parent / ".." / "test_config.json",
            Path(__file__).parent.parent.parent / "test_config.json",
        ]

        for config_path in config_paths:
            if config_path.exists():
                try:
                    with open(config_path, 'r', encoding='utf-8') as f:
                        loaded = json.load(f)
                        self._config.update(loaded)
                        # 更新 BASE_URL 到 tool 模块
                        if "base_url" in loaded:
                            import tool
                            tool.BASE_URL = loaded["base_url"]
                        break
                except Exception as e:
                    print(f"加载配置文件失败 {config_path}: {e}")
                    continue

    def get(self, key, default=None):
        return self._config.get(key, default)

    @property
    def test_stocks(self) -> List[str]:
        return self._config.get("test_stocks", self.DEFAULT_CONFIG["test_stocks"])

    @property
    def default_test_stock(self) -> str:
        return self._config.get("default_test_stock", self.DEFAULT_CONFIG["default_test_stock"])

    @property
    def default_quantity(self) -> int:
        return self._config.get("default_quantity", self.DEFAULT_CONFIG["default_quantity"])

    @property
    def order_status_timeout(self) -> int:
        return self._config.get("order_status_timeout", self.DEFAULT_CONFIG["order_status_timeout"])

    @property
    def order_poll_interval(self) -> float:
        return self._config.get("order_poll_interval", self.DEFAULT_CONFIG["order_poll_interval"])

    @property
    def skip_cleanup(self) -> bool:
        return self._config.get("skip_cleanup", self.DEFAULT_CONFIG["skip_cleanup"])

    @property
    def test_scenarios(self) -> List[Dict]:
        return self._config.get("test_scenarios", self.DEFAULT_CONFIG["test_scenarios"])


# 全局配置实例
config = TestConfig()

# ==================== 订单状态常量 ====================
class OrderStatus:
    """订单状态枚举（与 C++ OrderStatus 对应）"""
    UNKNOWN = 0           # OrderUnknow
    ACCEPTED = 1          # OrderAccept - 已报
    REJECTED = 2          # OrderReject - 已拒
    PARTIALLY_FILLED = 3  # OrderPartSuccess - 部分成交
    FILLED = 4            # OrderSuccess - 完全成交
    FAILED = 5            # OrderFail - 失败
    CANCEL_PARTIAL = 6    # CancelPartSuccess - 部分撤单
    CANCELLED = 7         # CancelSuccess - 已撤销
    CANCEL_FAILED = 8     # CancelFail - 撤单失败
    PARTIAL_CANCELLED = 9 # PartSuccessCancel - 部分成交已撤
    CACHED = 10           # OrderCached - 预埋单
    PRIVILEGE_REJECT = 11 # PrivilegeReject - 无权限
    NET_INTERRUPT = 12    # NetInterrupt - 网络中断

    @classmethod
    def is_active(cls, status: int) -> bool:
        """判断订单是否处于活动状态（可撤）"""
        return status in [cls.ACCEPTED, cls.PARTIALLY_FILLED, cls.CACHED]

    @classmethod
    def is_final(cls, status: int) -> bool:
        """判断订单是否处于最终状态（不可变）"""
        return status in [cls.FILLED, cls.FAILED, cls.CANCELLED,
                          cls.CANCEL_FAILED, cls.REJECTED, cls.PRIVILEGE_REJECT]

    @classmethod
    def get_name(cls, status: int) -> str:
        """获取状态名称"""
        names = {
            cls.UNKNOWN: "UNKNOWN",
            cls.ACCEPTED: "ACCEPTED",
            cls.REJECTED: "REJECTED",
            cls.PARTIALLY_FILLED: "PARTIALLY_FILLED",
            cls.FILLED: "FILLED",
            cls.FAILED: "FAILED",
            cls.CANCEL_PARTIAL: "CANCEL_PARTIAL",
            cls.CANCELLED: "CANCELLED",
            cls.CANCEL_FAILED: "CANCEL_FAILED",
            cls.PARTIAL_CANCELLED: "PARTIAL_CANCELLED",
            cls.CACHED: "CACHED",
            cls.PRIVILEGE_REJECT: "PRIVILEGE_REJECT",
            cls.NET_INTERRUPT: "NET_INTERRUPT",
        }
        return names.get(status, f"UNKNOWN({status})")


# ==================== 工具函数 ====================
def wait_order_status(auth_token, order_sysid: str, expected_statuses: List[int],
                       timeout: int = None, interval: float = None) -> bool:
    """
    等待订单达到指定状态之一

    Args:
        auth_token: 认证 token
        order_sysid: 订单系统 ID
        expected_statuses: 期望的状态列表
        timeout: 超时时间（秒），默认使用配置值
        interval: 轮询间隔（秒），默认使用配置值

    Returns:
        bool: 是否在超时前达到期望状态
    """
    timeout = timeout or config.order_status_timeout
    interval = interval or config.order_poll_interval

    start_time = time.time()
    while time.time() - start_time < timeout:
        orders = get_stock_orders(auth_token)
        for order in orders:
            if order.get('sysID') == order_sysid:
                current_status = order.get('status')
                if current_status in expected_statuses:
                    return True
        time.sleep(interval)
    return False


def get_stock_orders(auth_token, order_type: int = 0) -> List[Dict]:
    """
    获取订单列表

    Args:
        auth_token: 认证 token
        order_type: 0=股票，1=期权

    Returns:
        订单列表
    """
    kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
    response = requests.get(f"{BASE_URL}/trade/order?type={order_type}", **kwargs)
    return check_response(response) or []


def cancel_order(auth_token, stock_id: str, sys_id: str) -> Dict:
    """
    取消订单

    Args:
        auth_token: 认证 token
        stock_id: 股票/订单 ID
        sys_id: 系统订单 ID

    Returns:
        取消响应
    """
    kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
    params = {'id': stock_id, 'sysID': sys_id, 'type': 0}
    response = requests.delete(f"{BASE_URL}/trade/order", json=params, **kwargs)
    return check_response(response)


def cleanup_orders(auth_token, exclude_sysids: Optional[List[str]] = None):
    """
    清理所有活动订单

    Args:
        auth_token: 认证 token
        exclude_sysids: 需要保留的订单 sysID 列表
    """
    if config.skip_cleanup:
        print("[INFO] 跳过订单清理（skip_cleanup=True）")
        return

    if exclude_sysids is None:
        exclude_sysids = []

    orders = get_stock_orders(auth_token)
    cleaned = 0

    for order in orders:
        sys_id = order.get('sysID', '')
        status = order.get('status', OrderStatus.UNKNOWN)

        if sys_id not in exclude_sysids and OrderStatus.is_active(status):
            try:
                result = cancel_order(auth_token, order.get('id', ''), sys_id)
                cleaned += 1
                print(f"[INFO] 清理订单：{sys_id}, 状态：{OrderStatus.get_name(status)}")
            except Exception as e:
                print(f"[WARN] 清理订单 {sys_id} 失败：{e}")

    if cleaned > 0:
        print(f"[INFO] 共清理 {cleaned} 个订单")


def get_stock_info(auth_token, code: str) -> Optional[Dict]:
    """
    获取股票行情

    Args:
        auth_token: 认证 token
        code: 股票代码

    Returns:
        股票行情数据
    """
    kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
    params = {"id": code}
    response = requests.get(f"{BASE_URL}/stocks/detail", params=params, **kwargs)
    return check_response(response)


def limit_order(auth_token, code: str, price: float, quantity: int,
                direct: int = 0, kind: int = 0) -> Dict:
    """
    限价单下单

    Args:
        auth_token: 认证 token
        code: 股票代码
        price: 价格
        quantity: 数量
        direct: 0=买入，1=卖出
        kind: 0=股票，1=期权

    Returns:
        订单响应
    """
    kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
    params = {
        "symbol": code,
        'type': 1,           # 限价单
        'quantity': quantity,
        'prices': price,
        'direct': direct,    # 0=买入，1=卖出
        'kind': kind,
        'timeType': 0        # 当日有效
    }
    response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
    return check_response(response)


def market_order(auth_token, code: str, quantity: int,
                 direct: int = 0, prices: float = 0, kind: int = 0) -> Dict:
    """
    市价单下单

    Args:
        auth_token: 认证 token
        code: 股票代码
        quantity: 数量
        direct: 0=买入，1=卖出
        prices: 保护价（可选，市价单可设 0 或较高值）
        kind: 0=股票，1=期权

    Returns:
        订单响应
    """
    kwargs = {'verify': False, 'headers': {'Authorization': auth_token}}
    params = {
        "symbol": code,
        'type': 0,           # 市价单
        'quantity': quantity,
        'prices': prices,
        'direct': direct,
        'kind': kind,
        'timeType': 0
    }
    response = requests.post(f"{BASE_URL}/trade/order", json=params, **kwargs)
    return check_response(response)


def calculate_limit_price(stock_info: Dict, price_offset: float = 0) -> float:
    """
    计算限价单价格

    Args:
        stock_info: 股票行情数据
        price_offset: 价格偏移量（相对于当前价）

    Returns:
        计算后的价格
    """
    cur_price = stock_info['price']
    lower_price = stock_info.get('lower', cur_price * 0.9)
    upper_price = stock_info.get('upper', cur_price * 1.1)

    if price_offset == 0:
        # 使用中间价，确保订单能被接受
        return (lower_price + upper_price) / 2
    else:
        # 使用偏移价格
        return cur_price + price_offset


# ==================== 测试场景执行器 ====================
class TestScenarioExecutor:
    """测试场景执行器"""

    def __init__(self, auth_token):
        self.auth_token = auth_token
        self.created_orders = []

    def execute_step(self, step: Dict, stock_code: str) -> bool:
        """
        执行单个步骤

        Args:
            step: 步骤配置
            stock_code: 股票代码

        Returns:
            bool: 执行是否成功
        """
        operation = step.get('operation', '')
        count = step.get('count', config.default_quantity)
        price_offset = step.get('price_offset', 0)
        expected_status = step.get('expected_status')
        condition = step.get('condition', None)

        stock_info = get_stock_info(self.auth_token, stock_code)
        assert stock_info is not None, f"无法获取 {stock_code} 行情"

        if operation == 'buy':
            price = calculate_limit_price(stock_info, price_offset)
            result = limit_order(self.auth_token, stock_code, price, count, direct=0)

            assert result is not None, "买入订单响应为空"
            assert "id" in result, "响应缺少订单 ID"
            assert result['id'] != -1, "订单 ID 无效"
            assert "sysID" in result, "响应缺少系统订单 ID"

            self.created_orders.append(result['sysID'])

            if expected_status is not None:
                if isinstance(expected_status, list):
                    statuses = expected_status
                else:
                    statuses = [expected_status]

                time.sleep(0.5)  # 等待订单录入
                orders = get_stock_orders(self.auth_token)
                order_found = False
                for order in orders:
                    if order.get('sysID') == result['sysID']:
                        order_found = True
                        assert order.get('status') in statuses, \
                            f"订单状态 {OrderStatus.get_name(order.get('status'))} 不在期望列表 {statuses}"
                        break
                assert order_found, f"订单 {result['sysID']} 未在订单列表中找到"

            return True

        elif operation == 'sell':
            price = calculate_limit_price(stock_info, price_offset)
            result = limit_order(self.auth_token, stock_code, price, count, direct=1)

            assert result is not None, "卖出订单响应为空"
            assert "id" in result, "响应缺少订单 ID"
            assert result['id'] != -1, "订单 ID 无效"

            self.created_orders.append(result['sysID'])
            return True

        elif operation == 'market_buy':
            result = market_order(self.auth_token, stock_code, count, direct=0)

            assert result is not None, "市价买入订单响应为空"
            assert "id" in result, "响应缺少订单 ID"
            assert result['id'] != -1, "订单 ID 无效"

            self.created_orders.append(result['sysID'])

            if expected_status is not None:
                statuses = expected_status if isinstance(expected_status, list) else [expected_status]
                time.sleep(1)  # 市价单需要更多时间成交
                orders = get_stock_orders(self.auth_token)
                for order in orders:
                    if order.get('sysID') == result['sysID']:
                        assert order.get('status') in statuses, \
                            f"市价单状态 {OrderStatus.get_name(order.get('status'))} 不在期望列表 {statuses}"
                        break
            return True

        elif operation == 'market_sell':
            result = market_order(self.auth_token, stock_code, count, direct=1)

            assert result is not None, "市价卖出订单响应为空"
            assert "id" in result, "响应缺少订单 ID"
            assert result['id'] != -1, "订单 ID 无效"

            self.created_orders.append(result['sysID'])
            return True

        elif operation == 'cancel':
            condition = step.get('condition', 'wait')

            if condition == 'wait':
                # 等待订单状态后取消
                time.sleep(0.5)

            orders = get_stock_orders(self.auth_token)
            assert len(orders) > 0, "没有可取消的订单"

            # 取消最新的订单
            latest_order = orders[-1]
            sys_id = latest_order.get('sysID', '')
            stock_id = latest_order.get('id', '')

            if sys_id:
                cancel_result = cancel_order(self.auth_token, stock_id, sys_id)

                if expected_status is not None:
                    statuses = expected_status if isinstance(expected_status, list) else [expected_status]
                    status_reached = wait_order_status(self.auth_token, sys_id, statuses)
                    assert status_reached, \
                        f"订单 {sys_id} 未在超时时间内达到预期状态 {statuses}"
            return True

        else:
            pytest.fail(f"未知操作：{operation}")
            return False

    def execute_scenario(self, scenario: Dict, stock_code: str = None) -> bool:
        """
        执行完整测试场景

        Args:
            scenario: 场景配置
            stock_code: 股票代码，默认使用配置值

        Returns:
            bool: 执行是否成功
        """
        stock_code = stock_code or config.default_test_stock
        steps = scenario.get('steps', [])

        print(f"\n{'='*60}")
        print(f"执行场景：{scenario.get('name', 'unnamed')}")
        print(f"股票代码：{stock_code}")
        print(f"步骤数：{len(steps)}")
        print(f"{'='*60}")

        for i, step in enumerate(steps):
            print(f"\n[步骤 {i+1}/{len(steps)}] 操作：{step.get('operation', 'unknown')}")
            try:
                self.execute_step(step, stock_code)
                print(f"  ✓ 执行成功")
            except AssertionError as e:
                print(f"  ✗ 执行失败：{e}")
                raise
            except Exception as e:
                print(f"  ✗ 异常：{e}")
                raise

        print(f"\n场景执行完成")
        return True


# ==================== 测试类 ====================
@pytest.mark.usefixtures("auth_token")
class TestStockOrder:
    """股票订单基础测试"""

    def setup_method(self, auth_token):
        """每个测试方法执行前调用"""
        self.auth_token = auth_token
        self.created_orders = []
        self.executor = TestScenarioExecutor(auth_token)

    def teardown_method(self, auth_token):
        """每个测试方法执行后调用 - 清理订单"""
        cleanup_orders(auth_token, exclude_sysids=self.created_orders)

    def test_buy_order_basic(self, auth_token):
        """测试基本限价买入订单"""
        stock_code = config.default_test_stock

        # 获取当前行情
        stock_info = get_stock_info(auth_token, stock_code)
        assert stock_info is not None, f"无法获取 {stock_code} 行情"

        # 计算订单价格
        order_price = calculate_limit_price(stock_info, price_offset=0)

        # 下单
        result = limit_order(auth_token, stock_code, order_price, config.default_quantity, direct=0)

        # 验证响应
        assert result is not None, "订单响应为空"
        assert "id" in result, "响应缺少订单 ID"
        assert result['id'] != -1, "订单 ID 无效"
        assert "sysID" in result, "响应缺少系统订单 ID"

        # 记录订单用于清理
        self.created_orders.append(result['sysID'])

        # 等待订单状态确认
        time.sleep(0.5)
        orders = get_stock_orders(auth_token)
        order_found = False
        for order in orders:
            if order.get('sysID') == result['sysID']:
                order_found = True
                assert order.get('status') in [
                    OrderStatus.ACCEPTED,
                    OrderStatus.PARTIALLY_FILLED,
                    OrderStatus.FILLED
                ], f"订单状态异常：{OrderStatus.get_name(order.get('status'))}"
                assert order.get('direct') == 0, "订单方向错误，应该是买入"
                break

        assert order_found, f"订单 {result['sysID']} 未在订单列表中找到"

    def test_cancel_order(self, auth_token):
        """测试取消订单"""
        stock_code = config.default_test_stock
        stock_info = get_stock_info(auth_token, stock_code)

        # 先下一个买单（使用较低价格，确保不立即成交）
        lower_price = stock_info.get('lower', stock_info['price'] * 0.9)
        result = limit_order(auth_token, stock_code, lower_price, config.default_quantity, direct=0)
        assert result is not None and result.get('id') != -1, "下单失败"

        order_id = result['id']
        sys_id = result['sysID']
        self.created_orders.append(sys_id)

        # 等待订单录入
        time.sleep(0.5)

        # 取消订单
        cancel_result = cancel_order(auth_token, stock_code, sys_id)

        # 等待取消生效
        status_reached = wait_order_status(
            auth_token, sys_id,
            [OrderStatus.CANCELLED, OrderStatus.CANCEL_FAILED]
        )

        assert status_reached, f"订单 {sys_id} 未在超时时间内达到预期状态"

        # 验证最终状态
        orders = get_stock_orders(auth_token)
        final_status = None
        for order in orders:
            if order.get('sysID') == sys_id:
                final_status = order.get('status')
                # 取消成功或取消失败都是合理的最终状态
                assert final_status in [
                    OrderStatus.CANCELLED,
                    OrderStatus.CANCEL_FAILED,
                    OrderStatus.FILLED,      # 可能已成交无法取消
                    OrderStatus.PARTIALLY_FILLED
                ], f"订单最终状态异常：{OrderStatus.get_name(final_status)}"
                break

    def test_market_order_buy(self, auth_token):
        """测试市价买入"""
        stock_code = config.default_test_stock

        # 市价单买入
        result = market_order(auth_token, stock_code, config.default_quantity, direct=0)

        assert result is not None, "市价单响应为空"
        assert "id" in result, "响应缺少订单 ID"
        assert result['id'] != -1, "订单 ID 无效"

        self.created_orders.append(result['sysID'])

        # 市价单通常会快速成交
        time.sleep(1)
        orders = get_stock_orders(auth_token)
        order_found = False
        for order in orders:
            if order.get('sysID') == result['sysID']:
                order_found = True
                # 市价单可能已成交或正在成交
                assert order.get('status') in [
                    OrderStatus.FILLED,           # 完全成交
                    OrderStatus.PARTIALLY_FILLED, # 部分成交
                    OrderStatus.ACCEPTED          # 已接受（可能正在处理）
                ], f"市价单状态异常：{OrderStatus.get_name(order.get('status'))}"
                break

        assert order_found, f"订单 {result['sysID']} 未在订单列表中找到"

    @pytest.mark.parametrize("stock_code", config.test_stocks)
    def test_get_orders(self, auth_token, stock_code):
        """测试获取订单列表"""
        orders = get_stock_orders(auth_token)

        # 基本验证
        assert isinstance(orders, list), "订单列表应该是数组"

        # 验证每个订单的字段
        required_fields = ['id', 'sysID', 'status', 'direct', 'quantity', 'prices']
        for order in orders:
            for field in required_fields:
                assert field in order, f"订单缺少必需字段：{field}"

            # 验证状态值有效性
            status = order.get('status')
            assert isinstance(status, int), f"订单状态应该是整数：{status}"
            assert 0 <= status <= 12, f"订单状态超出范围：{status}"

    def test_sell_order_basic(self, auth_token):
        """测试限价卖出订单（需要先有持仓）"""
        stock_code = config.default_test_stock
        stock_info = get_stock_info(auth_token, stock_code)

        assert stock_info is not None, f"无法获取 {stock_code} 行情"

        # 获取当前价格，设置较高的卖出价格增加成交概率
        cur_price = stock_info['price']
        upper_price = stock_info.get('upper', cur_price * 1.1)
        order_price = (cur_price + upper_price) / 2  # 使用中间偏上价格

        result = limit_order(auth_token, stock_code, order_price, config.default_quantity, direct=1)

        # 注意：如果没有持仓，订单可能会失败
        # 这里只验证接口响应格式，不验证实际成交
        if result is not None:
            assert "id" in result, "响应缺少订单 ID"
            # 如果返回错误 ID，说明委托被拒绝（可能是因为没有持仓）
            if result['id'] == -1:
                print(f"[INFO] 卖出订单被拒绝（可能无持仓）: sysID={result.get('sysID', '')}")
            else:
                assert "sysID" in result, "响应缺少系统订单 ID"
                self.created_orders.append(result['sysID'])
        else:
            print("[INFO] 卖出订单返回空（可能无持仓）")

    def test_order_status_constants(self, auth_token):
        """测试订单状态常量定义"""
        # 验证状态常量值
        assert OrderStatus.ACCEPTED == 1
        assert OrderStatus.CANCELLED == 7
        assert OrderStatus.FILLED == 4

        # 验证状态名称
        assert OrderStatus.get_name(1) == "ACCEPTED"
        assert OrderStatus.get_name(7) == "CANCELLED"

        # 验证状态判断
        assert OrderStatus.is_active(OrderStatus.ACCEPTED) is True
        assert OrderStatus.is_active(OrderStatus.FILLED) is False
        assert OrderStatus.is_final(OrderStatus.FILLED) is True
        assert OrderStatus.is_final(OrderStatus.ACCEPTED) is False


# ==================== 场景化测试 ====================
@pytest.mark.usefixtures("auth_token")
class TestStockOrderScenarios:
    """场景化订单测试"""

    def setup_method(self, auth_token):
        self.auth_token = auth_token
        self.executor = TestScenarioExecutor(auth_token)

    def teardown_method(self, auth_token):
        cleanup_orders(auth_token, exclude_sysids=self.executor.created_orders)

    @pytest.mark.skipif(
        len(config.test_scenarios) == 0,
        reason="配置文件中没有定义测试场景"
    )
    @pytest.mark.parametrize("scenario", config.test_scenarios, ids=lambda x: x.get('name', 'unnamed'))
    def test_scenario(self, auth_token, scenario):
        """
        参数化场景测试

        场景配置示例：
        {
            "name": "buy_and_cancel",
            "steps": [
                {"operation": "buy", "count": 100, "expected_status": 1},
                {"operation": "cancel", "condition": "wait", "expected_status": 7}
            ]
        }
        """
        self.executor.execute_scenario(scenario)
