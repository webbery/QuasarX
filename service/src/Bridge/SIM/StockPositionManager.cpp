#include "Bridge/SIM/StockPositionManager.h"
#include <cmath>

StockPositionManager::StockPositionManager()
    : _commissionRate(0.0003)
    , _stampTaxRate(0.001)
{
}

// ==================== 持仓操作 ====================

TradeFees StockPositionManager::Buy(symbol_t symbol, int64_t qty, double price) {
    if (qty <= 0 || price <= 0.0) {
        WARN("Invalid buy params: qty={}, price={}", qty, price);
        return TradeFees{};
    }
    std::lock_guard<std::mutex> lock(_mutex);
    double amount = qty * price;
    TradeFees fees = CalcFees(amount, false);

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

    // T+1 控制：记录当日买入
    _todayBuyQty[symbol] += qty;

    INFO("BUY symbol={} qty={} price={} fees={}", symbol, qty, price, fees.total());
    return fees;
}

StockPositionManager::SellResult StockPositionManager::Sell(symbol_t symbol, int64_t qty, double price) {
    SellResult result{};
    if (qty <= 0 || price <= 0.0) {
        WARN("Invalid sell params: qty={}, price={}", qty, price);
        return result;
    }
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        WARN("No position to sell: {}", symbol);
        return result;
    }

    StockPosition& pos = it->second;
    if (pos._qty < qty) {
        WARN("Insufficient position: have={} sell={}", pos._qty, qty);
        qty = pos._qty;  // 卖出全部持仓
    }

    // T+1 限制：当日买入不能当日卖出
    auto buyIt = _todayBuyQty.find(symbol);
    if (buyIt != _todayBuyQty.end() && buyIt->second > 0) {
        int64_t canSell = pos._qty - buyIt->second;
        if (canSell < qty) {
            WARN("T+1 restriction: can only sell {} of {} today (bought {} today)",
                 canSell, qty, buyIt->second);
            qty = std::max((int64_t)0, canSell);
        }
    }

    if (qty <= 0) {
        return result;
    }

    double amount = qty * price;
    result.fees = CalcFees(amount, true);
    result.proceeds = amount - result.fees.total();
    result.actualQty = qty;

    // 更新持仓
    pos._qty -= qty;
    if (pos._qty == 0) {
        _positions.erase(it);
    }

    INFO("SELL symbol={} qty={} price={} fees={}", symbol, qty, price, result.fees.total());
    return result;
}

TradeFees StockPositionManager::AdjustPosition(symbol_t symbol, int64_t delta, double price) {
    if (delta > 0) {
        return Buy(symbol, delta, price);
    } else if (delta < 0) {
        auto result = Sell(symbol, -delta, price);
        return result.fees;
    }
    return TradeFees{};
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

AccountAsset StockPositionManager::GetAsset() const {
    std::lock_guard<std::mutex> lock(_mutex);
    AccountAsset asset{};
    asset.buying_power = 0.0;  // 资金由外部管理
    asset.total_asset = 0.0;   // 需要调用方计算持仓市值
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

void StockPositionManager::Reset() {
    std::lock_guard<std::mutex> lock(_mutex);
    _positions.clear();
    _todayBuyQty.clear();
}

void StockPositionManager::OnDayChange() {
    std::lock_guard<std::mutex> lock(_mutex);
    _todayBuyQty.clear();
}

// ==================== 内部方法 ====================

TradeFees StockPositionManager::CalcFees(double amount, bool isSell) const {
    TradeFees fees;
    fees.commission = amount * _commissionRate;
    fees.stampTax = isSell ? (amount * _stampTaxRate) : 0.0;
    return fees;
}
