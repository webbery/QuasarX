#pragma once
#include <mutex>
#include <map>
#include <string>
#include "std_header.h"

// 策略级资金池管理器
// 职责：管理总资金分配、策略资金隔离、回收、持久化
// 归属：BrokerSubSystem

struct StrategyCapitalInfo {
    double allocated = 0.0;   // 分配总额
    double available = 0.0;   // 当前可用（随交易变化）
    bool active = false;      // 策略是否活跃（暂停/运行时为 true）

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

    // 回收策略资金（策略完全停止时调用）
    // 返回实际回收的可用资金
    double reclaim(const String& strategy);

    // 标记策略为非活跃（暂停时调用，不回收资金）
    void deactivate(const String& strategy);

    // 更新策略可用资金（成交时调用，delta 为负表示资金被占用）
    void updateAvailable(const String& strategy, double delta);

    // 查询策略资金信息
    StrategyCapitalInfo get(const String& strategy) const;

    // 查询策略可用资金（快捷方法）
    double getAvailable(const String& strategy) const;

    // 查询策略已用资金（持仓市值 + 费用占用）
    double getUsed(const String& strategy) const;

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
