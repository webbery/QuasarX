#pragma once
#include "std_header.h"
#include "Util/system.h"   // Position / Fill / symbol_t
#include "Bridge/LedgerStore.h"
#include <functional>
#include <memory>

// 保证金账户配置
struct MarginConfig {
    double _initialMarginRatio = 1.0;      // 初始保证金比例（1.0 = 现金账户，不融资）
    double _maintenanceMarginRatio = 0.3;  // 维持保证金比例（30%）
    double _financingRate = 0.08;          // 融资利率（年化 8%）
    double _securitiesLendingRate = 0.10;  // 融券费率（年化 10%）
    double _warningRatio = 1.5;            // 警戒线（维持担保比例 150%）
    double _liquidationRatio = 1.3;        // 平仓线（维持担保比例 130%）
    bool _allowShort = false;              // 是否允许做空
};

// 保证金账户（管理两融相关的计算）
class MarginAccount {
public:
    MarginAccount() = default;
    explicit MarginAccount(const MarginConfig& config) : _config(config) {}

    // 配置访问
    const MarginConfig& getConfig() const { return _config; }
    void setConfig(const MarginConfig& config) { _config = config; }

    // 保证金计算
    double calculateInitialMargin(const Position& pos) const {
        if (!pos._isMargin) return std::abs(pos.getMarketValue());
        return std::abs(pos.getMarketValue()) * _config._initialMarginRatio;
    }

    double calculateMaintenanceMargin(const Position& pos) const {
        if (!pos._isMargin) return 0;
        return std::abs(pos.getMarketValue()) * _config._maintenanceMarginRatio;
    }

    // 购买力计算
    double calculateBuyingPower(double equity, double maintenanceMargin) const {
        double excess = equity - maintenanceMargin;
        if (excess <= 0) return 0;
        return excess / _config._initialMarginRatio;
    }

    // 做空购买力
    double calculateShortSellingPower(double equity) const {
        if (!_config._allowShort) return 0;
        return equity * 0.5; // 简化：假设做空比例为 50%
    }

    // 保证金追缴检查
    bool checkMarginCall(double equity, double maintenanceMargin) const {
        return equity < maintenanceMargin;
    }

    // 利息计算（按日计息）
    double calculateDailyInterest(double borrowedCash) const {
        return borrowedCash * _config._financingRate / 365.0;
    }

    double calculateDailySecuritiesFee(double borrowedValue) const {
        return borrowedValue * _config._securitiesLendingRate / 365.0;
    }

private:
    MarginConfig _config;
};

// 持仓只读快照。
// getAll() 按值返回：调用方普遍写成 capital->getPositions().getAll()，
// 若这里返回内部成员的引用，外层 getPositions() 返回临时对象时该引用会悬垂。
class PositionLedger {
public:
    void Set(const Map<symbol_t, Position>& positions) { _positions = positions; }

    Position getPosition(symbol_t symbol) const {
        auto it = _positions.find(symbol);
        return it != _positions.end() ? it->second : Position{};
    }

    int64_t getHoldingQuantity(symbol_t symbol) const {
        auto it = _positions.find(symbol);
        return it != _positions.end() ? it->second._quantity : 0;
    }

    Map<symbol_t, Position> getAll() const { return _positions; }

private:
    Map<symbol_t, Position> _positions;
};

// 策略资金接口
// 账本（LedgerStore）是资金与持仓的唯一来源，本接口是它的只读投影。
// 这里刻意不做缓存：早期版本用"版本号 + 本地副本"缓存投影，
// 而版本号协议存在漏写路径（Load / UpdatePrice 不 bump），
// 重启后投影会一直停在"空持仓"状态 —— 表现为策略把已持仓标的再买一遍、
// 预算按全量权益计算（超配）。直读后"重启即同步"是自然结果，不依赖失效通知。
class StrategyCapital {
public:
    virtual ~StrategyCapital() = default;

    // 资金查询
    virtual double getAllocated() const = 0;   // 分配总额
    virtual double getAvailable() const = 0;   // 可用现金（超配时为负，原样返回）
    virtual double getEquity() const = 0;      // 权益 = 可用 + 持仓市值 − 融资负债

    // 两融配置
    virtual void setMarginConfig(const MarginConfig& config) = 0;
    virtual const MarginConfig& getMarginConfig() const = 0;

    // 两融相关
    virtual double getMarginEquity() const = 0;        // 保证金权益
    virtual double getMaintenanceMargin() const = 0;   // 维持保证金
    virtual double getBuyingPower() const = 0;         // 购买力
    virtual double getShortSellingPower() const = 0;   // 做空购买力

    // 持仓查询
    virtual PositionLedger getPositions() const = 0;               // 整表快照（按值）
    virtual int64_t getHoldingQuantity(symbol_t symbol) const = 0; // 单只数量（O(1)）
    virtual Position getPosition(symbol_t symbol) const = 0;       // 单只持仓（O(1)）
    // 建仓时间（最早未平批次）；无记录返回 0
    virtual time_t getPositionEntryTime(symbol_t symbol) const = 0;

    // 预算计算（核心方法）
    // budget = equity × positionRatio − 保留持仓市值
    //   sellSymbols: 今日将卖出的标的；这些持仓不计入保留市值（其市值会以现金形式释放）
    virtual double SpendableBudget(double positionRatio, bool allowShort,
                                   const Set<symbol_t>& sellSymbols) const = 0;

    // 更新当前价格
    virtual void updatePrice(symbol_t symbol, double price) = 0;
};

// 策略资金实现：直读账本，不做本地缓存
class StrategyCapitalImpl : public StrategyCapital {
public:
    StrategyCapitalImpl(LedgerStore* ledger, const String& strategy,
                        double allocated, const MarginConfig& marginConfig = MarginConfig{})
        : _ledger(ledger), _strategy(strategy), _allocated(allocated), _margin(marginConfig) {}

    // 资金查询
    double getAllocated() const override {
        return _ledger ? _ledger->GetAllocated(_strategy) : _allocated;
    }

    double getAvailable() const override {
        // 原样返回账本现金：负值代表超额占用（已买入的市值超过可用资金），
        // 截成 0 会把超配藏起来，报表上只剩一个没有解释的"可用 ¥0"。
        return _ledger ? _ledger->GetAvailable(_strategy) : _allocated;
    }

    double getEquity() const override {
        if (_ledger) return _ledger->GetEquity(_strategy);
        return _allocated;
    }

    // 两融配置
    void setMarginConfig(const MarginConfig& config) override {
        _margin.setConfig(config);
    }

    const MarginConfig& getMarginConfig() const override {
        return _margin.getConfig();
    }

    // 两融相关
    double getMarginEquity() const override {
        if (_ledger) return _ledger->GetMarginEquity(_strategy);
        return _allocated;
    }

    double getMaintenanceMargin() const override {
        double total = 0.0;
        for (const auto& [symbol, pos] : getPositions().getAll()) {
            total += _margin.calculateMaintenanceMargin(pos);
        }
        return total;
    }

    double getBuyingPower() const override {
        return _margin.calculateBuyingPower(getMarginEquity(), getMaintenanceMargin());
    }

    double getShortSellingPower() const override {
        return _margin.calculateShortSellingPower(getMarginEquity());
    }

    // 持仓查询
    PositionLedger getPositions() const override {
        PositionLedger ledger;
        if (_ledger) ledger.Set(_ledger->GetPositions(_strategy));
        return ledger;
    }

    int64_t getHoldingQuantity(symbol_t symbol) const override {
        return _ledger ? _ledger->GetQuantity(_strategy, symbol) : 0;
    }

    Position getPosition(symbol_t symbol) const override {
        return _ledger ? _ledger->GetPosition(_strategy, symbol) : Position{};
    }

    time_t getPositionEntryTime(symbol_t symbol) const override {
        return _ledger ? _ledger->GetPositionEntryTime(_strategy, symbol) : 0;
    }

    // 预算计算
    double SpendableBudget(double positionRatio, bool allowShort,
                           const Set<symbol_t>& sellSymbols) const override {
        if (allowShort) {
            return getBuyingPower();
        }

        double equity = getEquity();
        double retainedValue = 0.0;
        // 直读账本：保留市值要用当前 bar 的价格（调用方会先把本 bar 价格灌进账本），
        // 读缓存会拿到上一版价格，预算就落在旧价上。
        const Map<symbol_t, Position> positions = _ledger
            ? _ledger->GetPositions(_strategy)
            : Map<symbol_t, Position>{};
        for (const auto& [symbol, pos] : positions) {
            if (pos._quantity == 0) continue;
            if (sellSymbols.count(symbol) == 0) {
                retainedValue += std::abs(pos.getMarketValue());
            }
        }

        double budget = equity * positionRatio - retainedValue;
        return std::max(0.0, budget);
    }

    // 更新当前价格
    void updatePrice(symbol_t symbol, double price) override {
        if (_ledger) {
            _ledger->UpdatePrice(_strategy, symbol, price);
        }
    }

private:
    LedgerStore* _ledger = nullptr;
    String _strategy;
    double _allocated = 0.0;
    MarginAccount _margin;
};
