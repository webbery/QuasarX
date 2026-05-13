#include "Bridge/SIM/StockPositionManager.h"
#include "Bridge/SIM/BacktestContext.h"
#include <cmath>

StockPositionManager::StockPositionManager(double initialCapital)
    : _availableFunds(initialCapital)
    , _initialCapital(initialCapital)
    , _commissionRate(0.0003)
    , _stampTaxRate(0.001)
    , _backtestMode(false)
{
}

// ==================== 持仓操作 ====================

void StockPositionManager::Buy(symbol_t symbol, int64_t qty, double price) {
    if (qty <= 0 || price <= 0.0) {
        WARN("Invalid buy params: qty={}, price={}", qty, price);
        return;
    }
    std::lock_guard<std::mutex> lock(_mutex);
    double amount = qty * price;
    double fees = CalcFees(amount, false);

    if (!_backtestMode) {
        if (_availableFunds < amount + fees) {
            WARN("Insufficient funds: need={}+{} avail={}", amount, fees, _availableFunds);
            return;
        }
        _availableFunds -= (amount + fees);
    }

    // 更新持仓（加权平均成本）
    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        StockPosition pos;
        pos._symbol = symbol;
        pos._qty = qty;
        pos._cost = price;
        pos._totalCost = amount;
        _positions[symbol] = pos;
    } else {
        StockPosition& pos = it->second;
        int64_t newQty = pos._qty + qty;
        pos._totalCost += amount;
        pos._cost = (newQty != 0) ? (pos._totalCost / newQty) : 0.0;
        pos._qty = newQty;
    }

    INFO("BUY symbol={} qty={} price={} fees={}", symbol, qty, price, fees);
}

void StockPositionManager::Sell(symbol_t symbol, int64_t qty, double price) {
    if (qty <= 0 || price <= 0.0) {
        WARN("Invalid sell params: qty={}, price={}", qty, price);
        return;
    }
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        WARN("No position to sell: {}", symbol);
        return;
    }

    StockPosition& pos = it->second;
    if (pos._qty < qty) {
        WARN("Insufficient position: have={} sell={}", pos._qty, qty);
        qty = pos._qty;  // 卖出全部持仓
    }

    double amount = qty * price;
    double fees = CalcFees(amount, true);

    if (!_backtestMode) {
        _availableFunds += (amount - fees);
    }

    // 更新持仓
    pos._qty -= qty;
    if (pos._qty == 0) {
        _positions.erase(it);
    }

    INFO("SELL symbol={} qty={} price={} fees={}", symbol, qty, price, fees);
}

void StockPositionManager::AdjustPosition(symbol_t symbol, int64_t delta, double price) {
    if (delta > 0) {
        Buy(symbol, delta, price);
    } else if (delta < 0) {
        Sell(symbol, -delta, price);
    }
}

// ==================== 查询 ====================

int64_t StockPositionManager::GetPosition(symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        return 0;
    }
    return it->second._qty;
}

void StockPositionManager::SetPosition(symbol_t symbol, int64_t qty) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        if (qty != 0) {
            StockPosition pos;
            pos._symbol = symbol;
            pos._qty = qty;
            pos._cost = 0.0;
            pos._totalCost = 0.0;
            _positions[symbol] = pos;
        }
    } else {
        it->second._qty = qty;
        if (qty == 0) {
            _positions.erase(it);
        }
    }
}

double StockPositionManager::GetPositionCost(symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        return 0.0;
    }
    return it->second._cost;
}

double StockPositionManager::GetAvailableFunds() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _availableFunds;
}

AccountAsset StockPositionManager::GetAsset() const {
    std::lock_guard<std::mutex> lock(_mutex);
    AccountAsset asset{};
    asset.buying_power = _availableFunds;
    // TODO: 计算持仓市值
    asset.total_asset = _availableFunds;
    return asset;
}

bool StockPositionManager::GetPosition(AccountPosition& pos) const {
    std::lock_guard<std::mutex> lock(_mutex);
    pos._positions.clear();
    for (const auto& [symbol, sp] : _positions) {
        position_t p{};
        p._symbol = symbol;
        p._holds = sp._qty;
        p._validHolds = sp._qty;
        p._price = sp._cost;
        pos._positions.push_back(p);
    }
    return !pos._positions.empty();
}

// ==================== 配置 ====================

void StockPositionManager::SetInitialCapital(double capital) {
    std::lock_guard<std::mutex> lock(_mutex);
    _initialCapital = capital;
    _availableFunds = capital;
}

void StockPositionManager::Reset() {
    std::lock_guard<std::mutex> lock(_mutex);
    _positions.clear();
    _availableFunds = _initialCapital;
}

// ==================== 内部方法 ====================

double StockPositionManager::CalcFees(double amount, bool isSell) const {
    double commission = amount * _commissionRate;
    double stampTax = isSell ? (amount * _stampTaxRate) : 0.0;
    return commission + stampTax;
}
