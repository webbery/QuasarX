#pragma once
#include "std_header.h"
#include "Util/system.h"   // Position / Fill / symbol_t
#include <functional>
#include <memory>

// 保证金账户配置
struct MarginConfig {
    double _initialMarginRatio = 0.5;      // 初始保证金比例（50%）
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

// 持仓账本（管理持仓的增删查）
class PositionLedger {
public:
    PositionLedger() = default;
    
    // 查询
    Position getPosition(symbol_t symbol) const {
        auto it = _positions.find(symbol);
        return it != _positions.end() ? it->second : Position{};
    }
    
    int64_t getHoldingQuantity(symbol_t symbol) const {
        auto pos = getPosition(symbol);
        return pos._quantity;
    }
    
    const Map<symbol_t, Position>& getAll() const {
        return _positions;
    }
    
    List<Position> getLongPositions() const {
        List<Position> result;
        for (const auto& [symbol, pos] : _positions) {
            if (pos.isLong()) result.push_back(pos);
        }
        return result;
    }
    
    List<Position> getShortPositions() const {
        List<Position> result;
        for (const auto& [symbol, pos] : _positions) {
            if (pos.isShort()) result.push_back(pos);
        }
        return result;
    }
    
    double getTotalLongValue() const {
        double total = 0;
        for (const auto& [symbol, pos] : _positions) {
            if (pos.isLong()) total += pos.getMarketValue();
        }
        return total;
    }
    
    double getTotalShortValue() const {
        double total = 0;
        for (const auto& [symbol, pos] : _positions) {
            if (pos.isShort()) total += std::abs(pos.getMarketValue());
        }
        return total;
    }
    
    // 修改
    void addPosition(symbol_t symbol, int64_t quantity, double price, bool isMargin = false, double marginRatio = 1.0) {
        auto& pos = _positions[symbol];
        pos._symbol = symbol;
        
        // 更新平均成本
        if (pos._quantity == 0) {
            pos._avgPrice = price;
        } else if ((pos._quantity > 0 && quantity > 0) || (pos._quantity < 0 && quantity < 0)) {
            // 同方向加仓：加权平均
            double totalCost = pos._quantity * pos._avgPrice + quantity * price;
            pos._avgPrice = totalCost / (pos._quantity + quantity);
        }
        
        pos._quantity += quantity;
        pos._isMargin = isMargin;
        pos._marginRatio = marginRatio;
        
        // 清理空持仓
        if (pos._quantity == 0) {
            _positions.erase(symbol);
        }
    }
    
    void removePosition(symbol_t symbol, int64_t quantity) {
        addPosition(symbol, -quantity, 0);
    }
    
    void updateCurrentPrice(symbol_t symbol, double price) {
        auto it = _positions.find(symbol);
        if (it != _positions.end()) {
            it->second._currentPrice = price;
        }
    }
    
    void clear() {
        _positions.clear();
    }
    
private:
    Map<symbol_t, Position> _positions;
};

// 策略资金接口
class StrategyCapital {
public:
    virtual ~StrategyCapital() = default;
    
    // 资金查询
    virtual double getAllocated() const = 0;   // 分配总额（永不变化）
    virtual double getAvailable() const = 0;   // 可用余额（成交后变化）
    virtual double getEquity() const = 0;      // 权益 = 可用 + 持仓市值
    
    // 两融配置
    virtual void setMarginConfig(const MarginConfig& config) = 0;
    virtual const MarginConfig& getMarginConfig() const = 0;
    
    // 两融相关
    virtual double getMarginEquity() const = 0;        // 保证金权益
    virtual double getMaintenanceMargin() const = 0;   // 维持保证金
    virtual double getBuyingPower() const = 0;         // 购买力
    virtual double getShortSellingPower() const = 0;   // 做空购买力
    
    // 持仓查询
    virtual const PositionLedger& getPositions() const = 0;
    
    // 预算计算（核心方法）
    // budget = equity × positionRatio − 保留持仓市值
    //   sellSymbols: 今日将卖出的标的；这些持仓不计入保留市值（其市值会以现金形式释放）
    virtual double SpendableBudget(double positionRatio, bool allowShort,
                                   const Set<symbol_t>& sellSymbols) const = 0;
    
    // 成交回写
    virtual void recordFill(const Fill& fill) = 0;
    
    // 更新当前价格
    virtual void updatePrice(symbol_t symbol, double price) = 0;
};

// 策略资金实现
class StrategyCapitalImpl : public StrategyCapital {
public:
    StrategyCapitalImpl(double allocated, const MarginConfig& marginConfig = MarginConfig{})
        : _allocated(allocated), _available(allocated), _margin(marginConfig) {}
    
    // 资金查询
    double getAllocated() const override { return _allocated; }
    double getAvailable() const override { return std::max(0.0, _available); }
    
    double getEquity() const override {
        // 权益 = 现金 + 持仓市值 - 借入现金
        double cash = _available;
        double positionValue = _positions.getTotalLongValue() - _positions.getTotalShortValue();
        return cash + positionValue - _borrowedCash;
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
        // 保证金权益 = 现金 + 多头市值 - 空头市值 - 借入
        double cash = _available;
        double longValue = _positions.getTotalLongValue();
        double shortValue = _positions.getTotalShortValue();
        return cash + longValue - shortValue - _borrowedCash;
    }
    
    double getMaintenanceMargin() const override {
        // 维持保证金 = Σ(持仓市值 × 维持保证金比例)
        double total = 0;
        for (const auto& [symbol, pos] : _positions.getAll()) {
            total += _margin.calculateMaintenanceMargin(pos);
        }
        return total;
    }
    
    double getBuyingPower() const override {
        // 购买力 = (保证金权益 - 维持保证金) / 初始保证金比例
        double equity = getMarginEquity();
        double maintenance = getMaintenanceMargin();
        return _margin.calculateBuyingPower(equity, maintenance);
    }
    
    double getShortSellingPower() const override {
        // 做空购买力 = 保证金权益 × 做空比例
        double equity = getMarginEquity();
        return _margin.calculateShortSellingPower(equity);
    }
    
    // 持仓查询
    const PositionLedger& getPositions() const override { return _positions; }
    
    // 预算计算
    double SpendableBudget(double positionRatio, bool allowShort,
                           const Set<symbol_t>& sellSymbols) const override {
        if (allowShort) {
            // 允许做空：使用保证金购买力
            return getBuyingPower();
        }

        // 权益口径：目标总敞口 = equity × positionRatio，
        // 扣除今日不会被卖出的持仓市值，剩余即为可买入预算。
        double equity = getEquity();
        double retainedValue = 0.0;
        for (const auto& [symbol, pos] : _positions.getAll()) {
            if (pos._quantity == 0) continue;
            if (sellSymbols.count(symbol) == 0) {
                retainedValue += std::abs(pos.getMarketValue());
            }
        }

        double budget = equity * positionRatio - retainedValue;
        return std::max(0.0, budget);
    }
    
    // 成交回写
    void recordFill(const Fill& fill) override {
        if (fill.isBuy()) {
            if (fill._isMargin && !fill._isShortCover) {
                // 融资买入：借入现金
                double cost = fill.getGrossAmount();
                double margin = cost * fill._marginRatio;
                _borrowedCash += (cost - margin);
                _available -= margin;
                _available -= fill.getTotalFees();
            } else {
                // 普通买入或平空
                _available += fill.getNetAmount();
            }
            _positions.addPosition(fill._symbol, fill._quantity, fill._price, 
                                   fill._isMargin, fill._marginRatio);
        } else {
            if (fill._isShortSell) {
                // 融券卖出：借入股票
                _positions.addPosition(fill._symbol, fill._quantity, fill._price, 
                                       true, fill._marginRatio);
                _available += fill.getGrossAmount();
                _available -= fill.getTotalFees();
            } else {
                // 普通卖出
                _available += fill.getNetAmount();
                _positions.removePosition(fill._symbol, std::abs(fill._quantity));
            }
        }
        
        // 成交后回写资金池，保证持久化状态与内存一致
        if (_persistCb) {
            _persistCb(_available);
        }
    }
    
    // 更新当前价格
    void updatePrice(symbol_t symbol, double price) override {
        _positions.updateCurrentPrice(symbol, price);
    }
    
    // 直接设置可用余额（用于初始化或重置）
    void setAvailable(double amount) { _available = amount; }
    
    // 获取借入现金
    double getBorrowedCash() const { return _borrowedCash; }
    
    // 持久化回调：每次成交后把最新 available 回写到 CapitalPool
    void setPersistCallback(std::function<void(double available)> cb) {
        _persistCb = std::move(cb);
    }
    
    // 用 Broker 的真实持仓覆盖账本（日终执行前同步）
    void syncPositions(const Map<symbol_t, Position>& positions) {
        _positions.clear();
        for (const auto& [symbol, pos] : positions) {
            if (pos._quantity != 0) {
                _positions.addPosition(symbol, pos._quantity, pos._avgPrice,
                                       pos._isMargin, pos._marginRatio);
            }
        }
    }
    
private:
    double _allocated;           // 分配总额
    double _available;           // 可用现金余额
    double _borrowedCash = 0.0;  // 借入的现金（融资负债）
    PositionLedger _positions;   // 持仓账本
    MarginAccount _margin;       // 保证金账户
    std::function<void(double)> _persistCb;  // 持久化回调
};

// 成交处理器（封装成交回写逻辑）
class FillHandler {
public:
    explicit FillHandler(StrategyCapital& capital) : _capital(capital) {}
    
    // 买入
    void recordBuy(symbol_t symbol, int64_t quantity, double price, 
                   double commission = 0, double slippage = 0,
                   bool isMargin = false, double marginRatio = 1.0) {
        Fill fill;
        fill._symbol = symbol;
        fill._quantity = quantity;
        fill._price = price;
        fill._time = time(nullptr);
        fill._commission = commission;
        fill._slippage = slippage;
        fill._isMargin = isMargin;
        fill._marginRatio = marginRatio;
        _capital.recordFill(fill);
    }
    
    // 卖出
    void recordSell(symbol_t symbol, int64_t quantity, double price,
                    double commission = 0, double stampTax = 0, double slippage = 0,
                    bool isMargin = false, double marginRatio = 1.0) {
        Fill fill;
        fill._symbol = symbol;
        fill._quantity = -quantity;
        fill._price = price;
        fill._time = time(nullptr);
        fill._commission = commission;
        fill._stampTax = stampTax;
        fill._slippage = slippage;
        fill._isMargin = isMargin;
        fill._marginRatio = marginRatio;
        _capital.recordFill(fill);
    }
    
    // 融券卖出
    void recordShortSell(symbol_t symbol, int64_t quantity, double price,
                         double commission = 0, double securitiesLendingFee = 0,
                         double marginRatio = 0.5) {
        Fill fill;
        fill._symbol = symbol;
        fill._quantity = -quantity;
        fill._price = price;
        fill._time = time(nullptr);
        fill._commission = commission;
        fill._isMargin = true;
        fill._marginRatio = marginRatio;
        fill._isShortSell = true;
        fill._securitiesLendingRate = securitiesLendingFee;
        _capital.recordFill(fill);
    }
    
    // 买入平空
    void recordShortCover(symbol_t symbol, int64_t quantity, double price,
                          double commission = 0, double slippage = 0) {
        Fill fill;
        fill._symbol = symbol;
        fill._quantity = quantity;
        fill._price = price;
        fill._time = time(nullptr);
        fill._commission = commission;
        fill._slippage = slippage;
        fill._isShortCover = true;
        _capital.recordFill(fill);
    }
    
private:
    StrategyCapital& _capital;
};

