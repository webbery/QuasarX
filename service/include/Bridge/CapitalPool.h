#pragma once
#include <mutex>
#include <map>
#include <string>
#include "std_header.h"

// 策略级资金池管理器
// 职责：管理总资金分配、策略资金隔离、回收、持久化
// 归属：BrokerSubSystem

// 信用交易（融资融券）成交类型。
// 与 include/Bridge/exchange.h 的 CreditOrderType 同义，但放在这里是为了让
// CapitalPool 不必依赖整个 exchange.h（那个头会拖进交易所 SDK 类型）。
enum class CreditAction {
    None,        // 非信用成交（现金买入 / 现金卖出）
    MarginBuy,   // 融资买入：借入现金，持仓记为多头
    ShortSell,   // 融券卖出：借入股票，在借数量增加
    ShortCover,  // 买入平空：归还借券
};

// 策略级信用交易敞口。
//
// 挂在 CapitalPool 上而不是 StrategyCapital 上，两个原因：
//   1. CapitalPool 已经按策略分账、且已落盘（capital_pool.json），
//      信用敞口跟着它走就白拿持久化 —— 进程重启后融券在借数量不能丢，
//      否则日终邮件算出来的融券费是假的。
//   2. StrategyCapitalImpl::_available 是预算口径（SpendableBudget 的输入），
//      动它会连带改下单规模，属于另一个风险面，不该混进两融这次改动。
struct StrategyCreditState {
    double               borrowedCash = 0.0;  // 融资余额（借入现金合计）
    Map<String, int64_t> borrowedShares;      // 融券在借数量（按标的）
    Map<String, double>  shortAvgPrice;       // 开空均价（按标的，空头的成本价）
    Map<String, double>  financedAmount;      // 融资本金（按标的）
};

struct StrategyCapitalInfo {
    double allocated = 0.0;   // 分配总额
    double available = 0.0;   // 当前可用（随交易变化）
    bool active = false;      // 策略是否活跃（暂停/运行时为 true）
    StrategyCreditState credit;  // 信用交易敞口（两融）

    double used() const { return allocated - available; }
};

class CapitalPool {
public:
    CapitalPool() = default;
    explicit CapitalPool(double initialCapital);

    // 初始化（从 config.json 或持久化文件加载）
    void init(double initialCapital);

    // 分配资金给策略
    // 返回 false 表示资金不足
    bool allocate(const String& strategy, double requested);

    // 回收策略资金（策略完全停止、或回测上下文析构时调用）
    // 只把注册置为非活跃：不移除 entry、不清零 available（见 .cpp 中的说明）。重复调用安全。
    double reclaim(const String& strategy);

    // 回收所有策略资金（测试隔离 / 服务重置时使用）
    void reclaimAll();

    // 标记策略为非活跃（暂停时调用，不回收资金）
    void deactivate(const String& strategy);

    // 更新策略可用资金（成交时调用，delta 为负表示资金被占用）
    void updateAvailable(const String& strategy, double delta);

    // 直接设置策略可用资金（StrategyCapital 持久化回写时使用）
    // entry 不存在时创建，allocated 取 max(现有 allocated, available)
    void setAvailable(const String& strategy, double available);

    // 查询策略资金信息
    StrategyCapitalInfo get(const String& strategy) const;

    // 查询策略可用资金（快捷方法）
    double getAvailable(const String& strategy) const;

    // 判断策略是否已在池中注册（与 BackTestHandler 路径的对接检查，
    // 避免 StartDaily 路径重复 allocate 把已累积的资金覆盖回 initial_capital）
    bool hasStrategy(const String& strategy) const;

    // 查询策略已用资金（持仓市值 + 费用占用）
    double getUsed(const String& strategy) const;

    // 信用成交回写：更新两融敞口（融资余额 / 在借股数 / 开空均价）。
    // 融资买入按 marginRatio 拆成"自有保证金 + 借入现金"，借入现金计入融资余额。
    // 调用方保证同一笔成交只回写一次（这里没有幂等键）。
    void applyCreditFill(const String& strategy, const String& symbol,
                         CreditAction action, int64_t quantity, double price,
                         double marginRatio);

    // 查询策略信用敞口（策略未注册时返回空状态）
    StrategyCreditState getCreditState(const String& strategy) const;

    // 总览
    double getInitialCapital() const { return _initialCapital; }
    double getTotalAllocated() const;
    double getTotalAvailable() const;
    int getActiveStrategyCount() const;

    // 持久化
    bool persist(const String& filePath) const;
    bool load(const String& filePath);

private:
    double _initialCapital = 0.0;
    Map<String, StrategyCapitalInfo> _strategies;
    mutable std::mutex _mutex;
};
