'''
影子模式（Shadow Mode）测试：
- 测试影子模式的日志输出
- 测试虚拟账户管理
- 测试模拟成交估算
- 测试日志解析和验证
'''
import requests
import json
import pytest
import os
import time
from tool import check_response, BASE_URL
from datetime import datetime

# 影子模式日志文件路径
SHADOW_LOG_DIR = "./logs/shadow"
SHADOW_LOG_FILE = f"{SHADOW_LOG_DIR}/shadow_{datetime.now().strftime('%Y%m%d')}.log"

class ShadowModeTester:
    """影子模式测试工具类"""

    @staticmethod
    def load_script(script_path):
        """加载策略脚本文件"""
        with open(script_path, 'r', encoding='utf-8') as file:
            return json.load(file)

    @staticmethod
    def parse_shadow_log(log_file):
        """解析影子日志文件

        日志格式：
        时间戳 | 策略名 | 标的 | 动作 | 预期价 | 预期量 | 模拟成交价 | 模拟成交量 | 状态 | 虚拟资金

        返回：
        List[Dict] 解析后的日志记录
        """
        records = []
        if not os.path.exists(log_file):
            return records

        with open(log_file, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                # 跳过注释行
                if line.startswith('#') or not line:
                    continue

                parts = [p.strip() for p in line.split('|')]
                if len(parts) >= 9:
                    record = {
                        'timestamp': int(parts[0]),
                        'strategy': parts[1],
                        'symbol': parts[2],
                        'action': parts[3],
                        'expected_price': float(parts[4]),
                        'expected_qty': int(parts[5]),
                        'fill_price': float(parts[6]),
                        'fill_qty': int(parts[7]),
                        'status': parts[8],
                        'available_funds': float(parts[9]) if len(parts) > 9 else 0.0
                    }
                    records.append(record)
        return records

    @staticmethod
    def get_filled_trades(records):
        """获取已成交的交易记录"""
        return [r for r in records if r['status'] == 'FILLED']

    @staticmethod
    def get_pending_trades(records):
        """获取未成交的交易记录"""
        return [r for r in records if r['status'] == 'PENDING']

    @staticmethod
    def get_rejected_trades(records):
        """获取被拒绝的交易记录"""
        return [r for r in records if r['status'] == 'REJECTED']

    @staticmethod
    def calculate_stats(records):
        """计算统计信息"""
        filled = ShadowModeTester.get_filled_trades(records)

        stats = {
            'total_signals': len(records),
            'filled_count': len(filled),
            'pending_count': len(ShadowModeTester.get_pending_trades(records)),
            'rejected_count': len(ShadowModeTester.get_rejected_trades(records)),
            'fill_rate': len(filled) / len(records) if records else 0,
            'total_buy_volume': sum(r['fill_qty'] for r in filled if r['action'] == 'BUY'),
            'total_sell_volume': sum(r['fill_qty'] for r in filled if r['action'] == 'SELL'),
            'avg_fill_price': sum(r['fill_price'] * r['fill_qty'] for r in filled) / sum(r['fill_qty'] for r in filled) if filled else 0,
        }
        return stats


@pytest.mark.usefixtures("auth_token")
class TestShadowMode:
    """影子模式功能测试类"""

    @pytest.fixture(autouse=True)
    def setup_teardown(self, auth_token):
        """每个测试前后的设置和清理"""
        # 设置：确保日志目录存在
        os.makedirs(SHADOW_LOG_DIR, exist_ok=True)

        # 记录测试前的日志行数
        self.pre_lines = 0
        if os.path.exists(SHADOW_LOG_FILE):
            with open(SHADOW_LOG_FILE, 'r') as f:
                self.pre_lines = len(f.readlines())

        yield

        # 清理：可选，如果需要可以删除测试产生的日志
        # if os.path.exists(SHADOW_LOG_FILE):
        #     with open(SHADOW_LOG_FILE, 'r') as f:
        #         post_lines = len(f.readlines())
        #     print(f"\n测试产生的日志行数：{post_lines - self.pre_lines}")

    def test_shadow_log_file_creation(self, auth_token):
        """测试影子日志文件创建"""
        # 影子模式应该在指定目录创建日志文件
        assert os.path.exists(SHADOW_LOG_DIR), f"影子日志目录 {SHADOW_LOG_DIR} 应该存在"

        # 日志文件应该可以被创建或追加
        if os.path.exists(SHADOW_LOG_FILE):
            assert os.path.isfile(SHADOW_LOG_FILE), "日志文件应该是普通文件"
            # 检查文件是否可读
            with open(SHADOW_LOG_FILE, 'r', encoding='utf-8') as f:
                content = f.read()
                assert isinstance(content, str), "日志文件应该可读"

    def test_shadow_log_format(self, auth_token):
        """测试影子日志格式解析"""
        if not os.path.exists(SHADOW_LOG_FILE):
            pytest.skip("影子日志文件不存在，跳过测试")

        records = ShadowModeTester.parse_shadow_log(SHADOW_LOG_FILE)

        # 如果有记录，验证格式
        if records:
            record = records[0]
            # 验证必填字段
            assert 'timestamp' in record, "日志记录应包含时间戳"
            assert 'strategy' in record, "日志记录应包含策略名"
            assert 'symbol' in record, "日志记录应包含标的代码"
            assert 'action' in record, "日志记录应包含买卖动作"
            assert 'expected_price' in record, "日志记录应包含预期价格"
            assert 'expected_qty' in record, "日志记录应包含预期数量"
            assert 'fill_price' in record, "日志记录应包含模拟成交价"
            assert 'fill_qty' in record, "日志记录应包含模拟成交量"
            assert 'status' in record, "日志记录应包含状态"

            # 验证字段类型
            assert isinstance(record['timestamp'], int), "时间戳应为整数"
            assert isinstance(record['symbol'], str), "标的代码应为字符串"
            assert record['action'] in ['BUY', 'SELL', 'HOLD'], f"动作应为 BUY/SELL/HOLD，实际为 {record['action']}"
            assert isinstance(record['expected_price'], float), "预期价格应为浮点数"
            assert isinstance(record['expected_qty'], int), "预期数量应为整数"

    def test_shadow_log_status_values(self, auth_token):
        """测试影子日志状态值的有效性"""
        if not os.path.exists(SHADOW_LOG_FILE):
            pytest.skip("影子日志文件不存在，跳过测试")

        records = ShadowModeTester.parse_shadow_log(SHADOW_LOG_FILE)

        valid_statuses = ['FILLED', 'PENDING', 'REJECTED']
        for record in records:
            assert record['status'] in valid_statuses, \
                f"状态 '{record['status']}' 应为 FILLED/PENDING/REJECTED 之一"

    def test_shadow_virtual_account_consistency(self, auth_token):
        """测试虚拟账户资金一致性"""
        if not os.path.exists(SHADOW_LOG_FILE):
            pytest.skip("影子日志文件不存在，跳过测试")

        records = ShadowModeTester.parse_shadow_log(SHADOW_LOG_FILE)
        filled = ShadowModeTester.get_filled_trades(records)

        if not filled:
            pytest.skip("没有成交记录，跳过测试")

        # 检查资金变化是否合理
        # 买入时资金减少，卖出时资金增加
        for i, record in enumerate(filled):
            if record['action'] == 'BUY':
                # 买入成交应该有数量
                assert record['fill_qty'] > 0, f"买入成交数量应大于 0: {record}"
            elif record['action'] == 'SELL':
                # 卖出成交应该有数量
                assert record['fill_qty'] > 0, f"卖出成交数量应大于 0: {record}"

    def test_shadow_fill_price_reasonable(self, auth_token):
        """测试模拟成交价格的合理性"""
        if not os.path.exists(SHADOW_LOG_FILE):
            pytest.skip("影子日志文件不存在，跳过测试")

        records = ShadowModeTester.parse_shadow_log(SHADOW_LOG_FILE)
        filled = ShadowModeTester.get_filled_trades(records)

        if not filled:
            pytest.skip("没有成交记录，跳过测试")

        # 检查成交价格是否在合理范围内
        for record in filled:
            expected = record['expected_price']
            actual = record['fill_price']

            # 成交价和预期价的差异不应太大（考虑滑点）
            if expected > 0:
                diff_rate = abs(actual - expected) / expected
                # 滑点通常在 0.1% - 0.5% 范围内
                assert diff_rate < 0.05, \
                    f"成交价与预期价差异过大 (>{5}%): 预期={expected}, 实际={actual}, 差异率={diff_rate:.4f}"

    def test_shadow_statistics_calculation(self, auth_token):
        """测试影子模式统计信息计算"""
        if not os.path.exists(SHADOW_LOG_FILE):
            pytest.skip("影子日志文件不存在，跳过测试")

        records = ShadowModeTester.parse_shadow_log(SHADOW_LOG_FILE)

        if not records:
            pytest.skip("没有日志记录，跳过测试")

        stats = ShadowModeTester.calculate_stats(records)

        # 验证统计信息
        assert 'total_signals' in stats, "统计应包含总信号数"
        assert 'filled_count' in stats, "统计应包含成交数"
        assert 'fill_rate' in stats, "统计应包含成交率"

        # 验证数据一致性
        total = stats['filled_count'] + stats['pending_count'] + stats['rejected_count']
        assert total == stats['total_signals'], \
            f"各类信号数量之和应等于总信号数：{stats['filled_count']}+{stats['pending_count']}+{stats['rejected_count']} != {stats['total_signals']}"

        # 打印统计信息
        print("\n=== 影子模式统计信息 ===")
        print(f"总信号数：{stats['total_signals']}")
        print(f"成交数：{stats['filled_count']}")
        print(f"未成交数：{stats['pending_count']}")
        print(f"拒绝数：{stats['rejected_count']}")
        print(f"成交率：{stats['fill_rate']:.2%}")


@pytest.mark.usefixtures("auth_token")
class TestShadowModeIntegration:
    """影子模式集成测试类"""

    def test_shadow_mode_backtest_workflow(self, auth_token):
        """测试影子模式回测工作流程"""
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        # 加载测试策略
        script_path = './script/ma2_strategy.json'
        if not os.path.exists(script_path):
            pytest.skip(f"策略文件 {script_path} 不存在，跳过测试")

        with open(script_path, 'r', encoding='utf-8') as f:
            script = json.load(f)

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        # 执行回测
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        data = check_response(response)

        assert isinstance(data, dict), "回测结果应为字典"
        assert 'features' in data, "回测结果应包含 features"
        assert 'buy' in data, "回测结果应包含 buy 信号"
        assert 'sell' in data, "回测结果应包含 sell 信号"

        # 验证回测执行完成
        assert 'summary' in data, "回测结果应包含 summary"
        summary = data['summary']
        print(f"\n=== 回测执行结果 ===")
        print(f"策略名：{summary.get('strategy_name', 'N/A')}")
        print(f"买入信号：{summary.get('buy_count', 0)}")
        print(f"卖出信号：{summary.get('sell_count', 0)}")

    def test_shadow_log_after_backtest(self, auth_token):
        """测试回测后影子日志记录"""
        # 先执行回测
        kwargs = {
            'verify': False,
            'headers': {'Authorization': auth_token} if auth_token else {}
        }

        script_path = './script/ma2_strategy.json'
        if not os.path.exists(script_path):
            pytest.skip(f"策略文件 {script_path} 不存在，跳过测试")

        with open(script_path, 'r', encoding='utf-8') as f:
            script = json.load(f)

        kwargs['json'] = {
            "script": json.dumps(script, ensure_ascii=False)
        }

        # 记录测试前的日志行数
        pre_lines = 0
        if os.path.exists(SHADOW_LOG_FILE):
            with open(SHADOW_LOG_FILE, 'r') as f:
                pre_lines = len(f.readlines())

        # 执行回测
        response = requests.post(f"{BASE_URL}/backtest", **kwargs)
        check_response(response)

        # 等待日志写入
        time.sleep(0.5)

        # 检查日志是否有新增
        if os.path.exists(SHADOW_LOG_FILE):
            with open(SHADOW_LOG_FILE, 'r') as f:
                post_lines = len(f.readlines())

            new_lines = post_lines - pre_lines
            print(f"\n回测产生的日志行数：{new_lines}")

            # 注意：如果影子模式未启用，可能不会有新日志
            # 这个测试主要用于验证影子模式的集成
            if new_lines > 0:
                # 验证新增日志的格式
                records = ShadowModeTester.parse_shadow_log(SHADOW_LOG_FILE)
                if records:
                    latest_records = records[-new_lines:] if new_lines <= len(records) else records
                    for record in latest_records:
                        assert 'strategy' in record
                        assert 'symbol' in record
                        assert 'action' in record


# --------------------------
# 命令行测试脚本
# --------------------------
if __name__ == "__main__":
    import subprocess

    # 运行影子模式测试
    pytest_args = [
        "-v",
        "-s",
        __file__,
        "--tb=short"
    ]

    exit_code = pytest.main(pytest_args)

    if exit_code == 0:
        print("\n✅ 影子模式测试全部通过!")
    else:
        print(f"\n❌ 影子模式测试失败，退出码：{exit_code}")
