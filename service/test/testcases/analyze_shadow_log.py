'''
影子模式日志解析和分析工具
'''
import os
import sys
from datetime import datetime
from collections import defaultdict

class ShadowLogAnalyzer:
    """影子日志分析器"""

    def __init__(self, log_file):
        self.log_file = log_file
        self.records = []
        self.load()

    def load(self):
        """加载日志文件"""
        if not os.path.exists(self.log_file):
            print(f"警告：日志文件 {self.log_file} 不存在")
            return

        with open(self.log_file, 'r', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
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
                    self.records.append(record)

    def filter_by_strategy(self, strategy):
        """按策略筛选"""
        return [r for r in self.records if r['strategy'] == strategy]

    def filter_by_symbol(self, symbol):
        """按标的筛选"""
        return [r for r in self.records if r['symbol'] == symbol]

    def filter_by_status(self, status):
        """按状态筛选"""
        return [r for r in self.records if r['status'] == status]

    def filter_by_action(self, action):
        """按买卖动作筛选"""
        return [r for r in self.records if r['action'] == action]

    def get_filled_trades(self):
        """获取已成交记录"""
        return self.filter_by_status('FILLED')

    def get_pending_trades(self):
        """获取未成交记录"""
        return self.filter_by_status('PENDING')

    def get_rejected_trades(self):
        """获取被拒绝记录"""
        return self.filter_by_status('REJECTED')

    def calculate_metrics(self):
        """计算绩效指标"""
        filled = self.get_filled_trades()

        if not filled:
            return {
                'total_signals': len(self.records),
                'filled_count': 0,
                'pending_count': len(self.get_pending_trades()),
                'rejected_count': len(self.get_rejected_trades()),
                'fill_rate': 0.0,
                'win_rate': 0.0,
                'total_profit': 0.0,
            }

        # 按策略和标的分组
        group_key = lambda x: (x['strategy'], x['symbol'])
        groups = defaultdict(list)
        for trade in filled:
            groups[group_key(trade)].append(trade)

        total_profit = 0.0
        win_count = 0
        loss_count = 0

        # 计算每笔交易的盈亏
        for key, trades in groups.items():
            position = 0
            avg_cost = 0.0

            for trade in trades:
                if trade['action'] == 'BUY':
                    # 买入：更新持仓和成本
                    new_qty = trade['fill_qty']
                    new_cost = trade['fill_price'] * new_qty
                    if position > 0:
                        avg_cost = (avg_cost * position + new_cost) / (position + new_qty)
                    else:
                        avg_cost = new_cost
                    position += new_qty

                elif trade['action'] == 'SELL':
                    # 卖出：计算盈亏
                    sell_qty = trade['fill_qty']
                    sell_price = trade['fill_price']
                    profit = (sell_price - avg_cost) * sell_qty
                    total_profit += profit

                    if profit > 0:
                        win_count += 1
                    elif profit < 0:
                        loss_count += 1

                    position -= sell_qty
                    if position <= 0:
                        position = 0
                        avg_cost = 0.0

        total_trades = win_count + loss_count
        win_rate = win_count / total_trades if total_trades > 0 else 0.0

        return {
            'total_signals': len(self.records),
            'filled_count': len(filled),
            'pending_count': len(self.get_pending_trades()),
            'rejected_count': len(self.get_rejected_trades()),
            'fill_rate': len(filled) / len(self.records) if self.records else 0.0,
            'win_count': win_count,
            'loss_count': loss_count,
            'win_rate': win_rate,
            'total_profit': total_profit,
        }

    def print_summary(self):
        """打印摘要报告"""
        print("=" * 60)
        print("影子模式日志分析报告")
        print("=" * 60)
        print(f"日志文件：{self.log_file}")
        print(f"记录总数：{len(self.records)}")
        print()

        metrics = self.calculate_metrics()

        print("=== 信号统计 ===")
        print(f"  总信号数：{metrics['total_signals']}")
        print(f"  已成交：{metrics['filled_count']}")
        print(f"  未成交：{metrics['pending_count']}")
        print(f"  已拒绝：{metrics['rejected_count']}")
        print(f"  成交率：  {metrics['fill_rate']:.2%}")
        print()

        print("=== 交易绩效 ===")
        print(f"  盈利次数：{metrics['win_count']}")
        print(f"  亏损次数：{metrics['loss_count']}")
        print(f"  胜率：    {metrics['win_rate']:.2%}")
        print(f"  总盈亏：  {metrics['total_profit']:.2f}")
        print()

        # 按策略分组统计
        strategies = set(r['strategy'] for r in self.records)
        if len(strategies) > 1:
            print("=== 按策略统计 ===")
            for strategy in sorted(strategies):
                strat_records = self.filter_by_strategy(strategy)
                strat_filled = [r for r in strat_records if r['status'] == 'FILLED']
                print(f"  {strategy}: {len(strat_filled)} 笔成交")

        # 按标的分组统计
        symbols = set(r['symbol'] for r in self.records)
        if len(symbols) > 1:
            print("\n=== 按标的统计 ===")
            for symbol in sorted(symbols):
                sym_records = self.filter_by_symbol(symbol)
                sym_filled = [r for r in sym_records if r['status'] == 'FILLED']
                print(f"  {symbol}: {len(sym_filled)} 笔成交")

        print("=" * 60)


def main():
    """命令行入口"""
    if len(sys.argv) < 2:
        # 默认使用今天的日志文件
        today = datetime.now().strftime('%Y%m%d')
        log_file = f"./logs/shadow/shadow_{today}.log"
        print(f"未指定日志文件，使用默认文件：{log_file}")
    else:
        log_file = sys.argv[1]

    if not os.path.exists(log_file):
        print(f"错误：日志文件不存在：{log_file}")
        print("用法：python analyze_shadow_log.py [日志文件路径]")
        sys.exit(1)

    analyzer = ShadowLogAnalyzer(log_file)
    analyzer.print_summary()

    # 导出 CSV
    if len(sys.argv) > 2 and sys.argv[2] == '--csv':
        csv_file = log_file.replace('.log', '.csv')
        with open(csv_file, 'w', encoding='utf-8') as f:
            f.write("timestamp,strategy,symbol,action,expected_price,expected_qty,fill_price,fill_qty,status,available_funds\n")
            for r in analyzer.records:
                f.write(f"{r['timestamp']},{r['strategy']},{r['symbol']},{r['action']},{r['expected_price']},{r['expected_qty']},{r['fill_price']},{r['fill_qty']},{r['status']},{r['available_funds']}\n")
        print(f"\n已导出 CSV 文件：{csv_file}")


if __name__ == "__main__":
    main()
