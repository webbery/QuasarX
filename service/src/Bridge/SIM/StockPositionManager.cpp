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

    auto it = _positions.find(symbol);
    if (it == _positions.end() || it->second._quantity == 0) {
        // 开多
        StockPosition pos;
        pos._symbol = symbol;
        pos._quantity = qty;
        pos._avgPrice = price;
        pos._totalCost = amount;
        _positions[symbol] = pos;
        // T+1 控制：记录当日买入
        _todayBuyQty[symbol] += qty;
    } else if (it->second._quantity > 0) {
        // 多头加仓（加权平均成本）
        StockPosition& pos = it->second;
        int64_t newQty = pos._quantity + qty;
        pos._totalCost += amount;
        pos._avgPrice = (newQty != 0) ? (pos._totalCost / newQty) : 0.0;
        pos._quantity = newQty;
        // T+1 控制：记录当日买入
        _todayBuyQty[symbol] += qty;
    } else {
        // 买券还券：数量向 0 收敛，剩余空头的成本价不变
        StockPosition& pos = it->second;
        int64_t coverQty = std::min(qty, -pos._quantity);
        pos._quantity += coverQty;
        if (pos._quantity == 0) {
            _positions.erase(it);
        } else {
            // 保持不变量 _totalCost = _quantity * _avgPrice
            pos._totalCost = pos._quantity * pos._avgPrice;
        }

        // 还券后仍有剩余（coverQty < qty）→ 超出部分转为新开的多头
        int64_t remain = qty - coverQty;
        if (remain > 0) {
            StockPosition np;
            np._symbol = symbol;
            np._quantity = remain;
            np._avgPrice = price;
            np._totalCost = remain * price;
            _positions[symbol] = np;
            _todayBuyQty[symbol] += remain;
        }
        // 注意：买券还券不计入 _todayBuyQty —— 买回的股票不是"当日买入的多头"，
        // 计入会让当日卖出的可用量被算成 0（canSell = 持仓 - 当日买入）。
    }

    INFO("BUY symbol={} qty={} price={} fees={}", symbol, qty, price, fees.total());
    return fees;
}

StockPositionManager::SellResult StockPositionManager::Sell(symbol_t symbol, int64_t qty, double price, bool allowShort) {
    SellResult result{};
    if (qty <= 0 || price <= 0.0) {
        WARN("Invalid sell params: qty={}, price={}", qty, price);
        return result;
    }
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _positions.find(symbol);
    int64_t held = (it == _positions.end()) ? 0 : it->second._quantity;

    if (held == 0) {
        // ── 无多仓：卖出要么被拒，要么开出空仓 ──
        if (!allowShort) {
            WARN("No position to sell: {}", symbol);
            return result;
        }
        // 融券卖出：建立负仓。_avgPrice 记开空均价（正数），
        // 盈亏口径 getUnrealizedPnL() = qty * (current - avg)：
        // qty 为负、current 低于 avg 时为正 → 空头盈利符号正确。
        double amount = qty * price;
        result.fees = CalcFees(amount, true);
        result.proceeds = amount - result.fees.total();
        result.actualQty = qty;
        result.isShortOpen = true;

        StockPosition pos;
        pos._symbol = symbol;
        pos._quantity = -qty;
        pos._avgPrice = price;
        pos._totalCost = -qty * price;  // 空头成本为负（卖出所得）
        pos._isMargin = true;
        _positions[symbol] = pos;

        INFO("SHORT OPEN symbol={} qty={} price={} fees={}", symbol, qty, price,
             result.fees.total());
        return result;
    }

    if (held < 0) {
        // ── 已有空仓：本次卖出是加空 ──
        StockPosition& pos = it->second;
        int64_t newQty = pos._quantity - qty;
        double newAvg = ((-pos._quantity) * pos._avgPrice + qty * price) / ((-newQty));
        pos._avgPrice = newAvg;
        pos._quantity = newQty;
        pos._totalCost = pos._quantity * pos._avgPrice;

        double amount = qty * price;
        result.fees = CalcFees(amount, true);
        result.proceeds = amount - result.fees.total();
        result.actualQty = qty;

        INFO("SHORT ADD symbol={} qty={} price={} avg={}", symbol, qty, price, newAvg);
        return result;
    }

    // ── 有多仓：平多（含部分平仓）──
    StockPosition& pos = it->second;
    if (pos._quantity < qty) {
        WARN("Insufficient position: have={} sell={}", pos._quantity, qty);
        qty = pos._quantity;  // 卖出全部持仓
    }

    // T+1 限制：当日买入不能当日卖出
    auto buyIt = _todayBuyQty.find(symbol);
    if (buyIt != _todayBuyQty.end() && buyIt->second > 0) {
        int64_t canSell = pos._quantity - buyIt->second;
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
    pos._quantity -= qty;
    if (pos._quantity == 0) {
        _positions.erase(it);
    } else {
        // 保持不变量 _totalCost = _quantity * _avgPrice
        pos._totalCost = pos._quantity * pos._avgPrice;
    }

    INFO("SELL symbol={} qty={} price={} fees={}", symbol, qty, price, result.fees.total());
    return result;
}

TradeFees StockPositionManager::AdjustPosition(symbol_t symbol, int64_t delta, double price,
                                               bool allowShort) {
    if (delta > 0) {
        return Buy(symbol, delta, price);
    } else if (delta < 0) {
        auto result = Sell(symbol, -delta, price, allowShort);
        return result.fees;
    }
    return TradeFees{};
}

PositionEffect StockPositionManager::ApplyEvent(
    symbol_t symbol, const IPositionEvent& event, double currentPrice)
{
    std::lock_guard<std::mutex> lock(_mutex);

    auto it = _positions.find(symbol);
    int64_t curQty = (it != _positions.end()) ? it->second._quantity : 0;

    PositionEffect fx = event.computeEffect(curQty, currentPrice);

    if (fx.qtyDelta == 0 && fx.cashDelta == 0.0 && fx.costBasisDelta == 0.0)
        return fx;

    auto& pos = _positions[symbol];
    pos._symbol = symbol;
    pos._quantity += fx.qtyDelta;
    pos._totalCost += fx.costBasisDelta;

    if (pos._quantity > 0 && pos._totalCost > 0) {
        pos._avgPrice = pos._totalCost / pos._quantity;
    }
    if (pos._quantity == 0) {
        pos._avgPrice = 0;
        pos._totalCost = 0;
    }

    INFO("EVENT symbol={} qtyDelta={} cashDelta={} newQty={}",
         symbol, fx.qtyDelta, fx.cashDelta, pos._quantity);
    return fx;
}

// ==================== 查询 ====================

int64_t StockPositionManager::GetPosition(symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        return 0;
    }
    return it->second._quantity;
}

void StockPositionManager::SetPosition(symbol_t symbol, int64_t qty) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _positions.find(symbol);
    if (it == _positions.end()) {
        if (qty != 0) {
            StockPosition pos;
            pos._symbol = symbol;
            pos._quantity = qty;
            pos._avgPrice = 0.0;
            pos._totalCost = 0.0;
            _positions[symbol] = pos;
        }
    } else {
        it->second._quantity = qty;
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
    return it->second._avgPrice;
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
        p._quantity = sp._quantity;
        p._validQuantity = sp._quantity;
        p._avgPrice = sp._avgPrice;
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
