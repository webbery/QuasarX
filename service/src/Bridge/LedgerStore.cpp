#include "Bridge/LedgerStore.h"
#include "Util/log.h"
#include "Util/datetime.h"
#include <filesystem>
#include <fstream>

namespace {

const LedgerLots& EmptyLots() {
    static const LedgerLots kEmpty;
    return kEmpty;
}

// FIFO 消耗：扣减分批并返回消耗掉的成本
double ConsumeLots(LedgerLots& lots, symbol_t symbol, int64_t qty) {
    auto it = lots.find(symbol);
    if (it == lots.end()) return 0.0;

    double cost = 0.0;
    int64_t remaining = qty;
    auto& batch = it->second;
    while (remaining > 0 && !batch.empty()) {
        auto& lot = batch.front();
        int64_t take = std::min(remaining, lot._quantity);
        cost += static_cast<double>(take) * lot._price;
        lot._quantity -= take;
        remaining -= take;
        if (lot._quantity <= 0) {
            batch.pop_front();
        }
    }
    if (batch.empty()) {
        lots.erase(it);
    }
    return cost;
}

}  // namespace

// ── 内部工具 ────────────────────────────────────────────────────

LedgerStore::Account& LedgerStore::_account(const String& strategy) {
    return _accounts[strategy];
}

const LedgerStore::Account* LedgerStore::_find(const String& strategy) const {
    auto it = _accounts.find(strategy);
    return it == _accounts.end() ? nullptr : &it->second;
}

void LedgerStore::_touch(const String& strategy) {
    _versions[strategy] = _versions.count(strategy) ? _versions[strategy] + 1 : 1;
}

int64_t LedgerStore::QuantityOf(const Account& acc, symbol_t symbol) {
    auto it = acc._positions.find(symbol);
    return it == acc._positions.end() ? 0 : it->second._quantity;
}

// ── 资金分配 ────────────────────────────────────────────────────

bool LedgerStore::Allocate(const String& strategy, double amount) {
    std::lock_guard<std::mutex> lock(_mutex);
    Account& acc = _account(strategy);
    acc._allocated = amount;
    acc._cash = amount;
    acc._active = true;
    _touch(strategy);
    return true;
}

double LedgerStore::Reclaim(const String& strategy) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _accounts.find(strategy);
    if (it == _accounts.end()) return 0.0;
    it->second._active = false;
    _touch(strategy);
    return it->second._allocated;
}

void LedgerStore::Deactivate(const String& strategy) {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _accounts.find(strategy);
    if (it != _accounts.end()) {
        it->second._active = false;
        _touch(strategy);
    }
}

void LedgerStore::ReclaimAll() {
    std::lock_guard<std::mutex> lock(_mutex);
    _accounts.clear();
    _versions.clear();
}

bool LedgerStore::HasStrategy(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _accounts.count(strategy) > 0;
}

int LedgerStore::GetActiveStrategyCount() const {
    std::lock_guard<std::mutex> lock(_mutex);
    int n = 0;
    for (const auto& [name, acc] : _accounts) {
        if (acc._active) ++n;
    }
    return n;
}

double LedgerStore::GetTotalAllocated() const {
    std::lock_guard<std::mutex> lock(_mutex);
    double total = 0.0;
    for (const auto& [name, acc] : _accounts) {
        total += acc._allocated;
    }
    return total;
}

double LedgerStore::GetTotalAvailable() const {
    std::lock_guard<std::mutex> lock(_mutex);
    double total = 0.0;
    for (const auto& [name, acc] : _accounts) {
        total += acc._cash;
    }
    return total;
}

// ── 现金 ────────────────────────────────────────────────────────

double LedgerStore::GetAvailable(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    return acc ? acc->_cash : 0.0;
}

double LedgerStore::GetUsed(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    return acc ? acc->_allocated - acc->_cash : 0.0;
}

double LedgerStore::GetAllocated(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    return acc ? acc->_allocated : 0.0;
}

void LedgerStore::SetAvailable(const String& strategy, double available) {
    std::lock_guard<std::mutex> lock(_mutex);
    _account(strategy)._cash = available;
    _touch(strategy);
}

double LedgerStore::UpdateAvailable(const String& strategy, double delta) {
    std::lock_guard<std::mutex> lock(_mutex);
    Account& acc = _account(strategy);
    acc._cash += delta;
    if (acc._cash < 0.0) {
        WARN("[Ledger] strategy '{}' cash negative: {}", strategy, acc._cash);
    }
    _touch(strategy);
    return acc._cash;
}

// ── 账户 ────────────────────────────────────────────────────────

// 一个 LedgerStore 实例服务多个策略：每个策略一组 cash / positions / credit。
// TODO(账户模型)：尚未把"资金账户"抽成独立对象，接入券商后需要区分
// 券商资金账号与本地策略账本的多对多关系。
Map<String, LedgerStore::Account> LedgerStore::GetAccounts() const {
    std::lock_guard<std::mutex> lock(_mutex);
    return _accounts;
}

// ── 持仓 ────────────────────────────────────────────────────────

Map<symbol_t, Position> LedgerStore::GetPositions(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return {};
    return acc->_positions;
}

int64_t LedgerStore::GetQuantity(const String& strategy, symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return 0;
    return QuantityOf(*acc, symbol);
}

const LedgerLots& LedgerStore::GetLots(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return EmptyLots();
    return acc->_lots;
}

double LedgerStore::GetPositionCost(const String& strategy, symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return 0.0;
    auto it = acc->_positions.find(symbol);
    if (it == acc->_positions.end()) return 0.0;
    return static_cast<double>(it->second._quantity) * it->second._avgPrice;
}

double LedgerStore::GetPositionValue(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return 0.0;
    double value = 0.0;
    for (const auto& [symbol, pos] : acc->_positions) {
        value += pos.getMarketValue();
    }
    return value;
}

void LedgerStore::UpdatePrice(const String& strategy, symbol_t symbol, double price) {
    if (price <= 0.0) return;
    std::lock_guard<std::mutex> lock(_mutex);
    Account& acc = _account(strategy);
    auto it = acc._positions.find(symbol);
    if (it != acc._positions.end()) {
        it->second._currentPrice = price;
    }
}

bool LedgerStore::HasPosition(const String& strategy, symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return false;
    return QuantityOf(*acc, symbol) != 0;
}

// ── 两融 ────────────────────────────────────────────────────────

StrategyCreditState LedgerStore::GetCreditState(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return StrategyCreditState{};
    return acc->_credit;
}

double LedgerStore::GetBorrowedCash(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    return acc ? acc->_credit.borrowedCash : 0.0;
}

double LedgerStore::GetEquity(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return 0.0;
    double value = 0.0;
    for (const auto& [symbol, pos] : acc->_positions) {
        value += pos.getMarketValue();
    }
    return acc->_cash + value - acc->_credit.borrowedCash;
}

double LedgerStore::GetMarginEquity(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    if (!acc) return 0.0;
    double longValue = 0.0;
    double shortValue = 0.0;
    for (const auto& [symbol, pos] : acc->_positions) {
        if (pos._quantity > 0) {
            longValue += pos.getMarketValue();
        } else if (pos._quantity < 0) {
            shortValue += std::abs(pos.getMarketValue());
        }
    }
    return acc->_cash + longValue - shortValue - acc->_credit.borrowedCash;
}

// ── 盈亏 ────────────────────────────────────────────────────────

void LedgerStore::AddProfit(const String& strategy, double profit) {
    std::lock_guard<std::mutex> lock(_mutex);
    _account(strategy)._profit += profit;
}

double LedgerStore::GetProfit(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    const Account* acc = _find(strategy);
    return acc ? acc->_profit : 0.0;
}

// ── 写入口 1：成交回写 ──────────────────────────────────────────

double LedgerStore::ApplyFill(const String& strategy, const LedgerFill& fill) {
    if (fill._quantity == 0 || fill._price <= 0.0) return 0.0;

    std::lock_guard<std::mutex> lock(_mutex);
    Account& acc = _account(strategy);

    const bool isBuy = fill._quantity > 0;
    const int64_t qty = std::abs(fill._quantity);
    const double gross = std::abs(fill.Gross());
    const double fees = gross * fill.FeeRate() + fill._tradeFees;
    const String sym = get_symbol(fill._symbol);

    double cashDelta = 0.0;

    if (isBuy && fill._isShortCover) {
        // 买入平空：付现金并归还借券
        cashDelta = -(gross + fees);
        auto sharesIt = acc._credit.borrowedShares.find(sym);
        if (sharesIt != acc._credit.borrowedShares.end()) {
            sharesIt->second -= qty;
            if (sharesIt->second <= 0) {
                acc._credit.borrowedShares.erase(sharesIt);
                acc._credit.shortAvgPrice.erase(sym);
            }
        }
    } else if (isBuy) {
        // 买入开多：自有资金占用 + 融资差额
        const double ratio = std::clamp(fill._marginRatio, 0.0, 1.0);
        const double own = gross * ratio;
        const double borrow = gross - own;
        cashDelta = -(own + fees);
        if (borrow > 0.0) {
            acc._credit.borrowedCash += borrow;
            acc._credit.financedAmount[sym] += borrow;
        }
    } else if (fill._isShortSell) {
        // 融券卖出：现金进账，借券增加
        cashDelta = gross - fees;
        auto& shares = acc._credit.borrowedShares[sym];
        auto& avg = acc._credit.shortAvgPrice[sym];
        const double oldValue = avg * static_cast<double>(shares);
        shares += qty;
        avg = (oldValue + gross) / static_cast<double>(shares);
    } else {
        // 卖出平多：卖出所得先按比例偿还该标的的融资本金
        double repay = 0.0;
        auto finIt = acc._credit.financedAmount.find(sym);
        if (finIt != acc._credit.financedAmount.end() && finIt->second > 0.0) {
            const int64_t held = QuantityOf(acc, fill._symbol);
            if (held > 0) {
                repay = finIt->second * static_cast<double>(qty) / static_cast<double>(held);
            }
            repay = std::min(repay, finIt->second);
            repay = std::min(repay, acc._credit.borrowedCash);
            finIt->second -= repay;
            if (finIt->second <= 0.0) {
                acc._credit.financedAmount.erase(finIt);
            }
            acc._credit.borrowedCash -= repay;
        }
        cashDelta = gross - fees - repay;
    }

    // ── 持仓与 FIFO ──
    const int64_t delta = fill._quantity;
    const int64_t oldQty = QuantityOf(acc, fill._symbol);
    const double oldAvg = [&]() {
        auto it = acc._positions.find(fill._symbol);
        return it == acc._positions.end() ? 0.0 : it->second._avgPrice;
    }();

    int64_t closeQty = 0;
    int64_t openQty = 0;
    if (oldQty == 0) {
        openQty = delta;
    } else if ((oldQty > 0) == (delta > 0)) {
        openQty = delta;
    } else {
        closeQty = std::min(std::abs(delta), std::abs(oldQty));
        openQty = std::abs(delta) - closeQty;
    }

    double closedCost = 0.0;
    if (closeQty > 0) {
        closedCost = oldQty > 0 ? ConsumeLots(acc._lots, fill._symbol, closeQty)
                                : static_cast<double>(closeQty) * oldAvg;
    }

    {
        auto it = acc._positions.find(fill._symbol);
        if (it == acc._positions.end()) {
            Position pos{};
            pos._symbol = fill._symbol;
            pos._quantity = delta;
            pos._avgPrice = fill._price;
            pos._currentPrice = fill._price;
            pos._isMargin = fill._isMargin;
            pos._marginRatio = fill._marginRatio;
            acc._positions[fill._symbol] = pos;
        } else {
            Position& pos = it->second;
            const int64_t newQty = pos._quantity + delta;
            if (newQty == 0) {
                acc._positions.erase(it);
            } else {
                const bool flipped = (pos._quantity > 0) != (newQty > 0);
                if (flipped) {
                    pos._avgPrice = fill._price;
                } else if (openQty != 0) {
                    const double total = static_cast<double>(pos._quantity) * pos._avgPrice +
                                         static_cast<double>(delta) * fill._price;
                    pos._avgPrice = total / static_cast<double>(newQty);
                }
                pos._quantity = newQty;
                pos._currentPrice = fill._price;
            }
        }
    }

    // 多头建仓追加 FIFO 分批
    if (isBuy && !fill._isShortCover && openQty > 0 && (oldQty + delta) > 0) {
        LedgerLot lot{};
        lot._quantity = openQty;
        lot._price = fill._price;
        lot._date = fill._time;
        acc._lots[fill._symbol].push_back(lot);
    }

    // 已实现盈亏
    if (!isBuy && !fill._isShortSell && closedCost > 0.0) {
        acc._profit += gross - closedCost - fees;
    } else if (isBuy && fill._isShortCover && closeQty > 0) {
        acc._profit += (oldAvg - fill._price) * static_cast<double>(closeQty) - fees;
    }

    acc._cash += cashDelta;
    if (acc._cash < 0.0) {
        WARN("[Ledger] strategy '{}' cash negative after fill: {}", strategy, acc._cash);
    }
    _touch(strategy);
    return cashDelta;
}

// ── 写入口 2：券商账本同步 ──────────────────────────────────────

bool LedgerStore::SyncBrokerMirror(const String& strategy, const String& provider,
                                   double cash, double borrowedCash,
                                   const Map<String, int64_t>& borrowedShares,
                                   const Map<symbol_t, Position>& positions) {
    std::lock_guard<std::mutex> lock(_mutex);

    BrokerMirror mirror;
    mirror._valid = true;
    mirror._provider = provider;
    mirror._syncedAt = time(nullptr);
    mirror._cash = cash;
    mirror._borrowedCash = borrowedCash;
    mirror._borrowedShares = borrowedShares;
    mirror._positions = positions;

    const Account* acc = _find(strategy);
    if (acc) {
        mirror._cashDiff = acc->_cash - cash;
        Set<symbol_t> symbols;
        for (const auto& [symbol, pos] : acc->_positions) symbols.insert(symbol);
        for (const auto& [symbol, pos] : positions) symbols.insert(symbol);
        for (const auto& symbol : symbols) {
            const int64_t localQty = QuantityOf(*acc, symbol);
            auto brokerIt = positions.find(symbol);
            const int64_t brokerQty = brokerIt == positions.end() ? 0 : brokerIt->second._quantity;
            if (localQty != brokerQty) {
                mirror._positionDiff.push_back(
                    fmt::format("{}: local={} broker={}", get_symbol(symbol), localQty, brokerQty));
            }
        }
    }
    mirror._matched = std::abs(mirror._cashDiff) < 0.01 && mirror._positionDiff.empty();

    _mirrors[strategy] = std::move(mirror);
    return true;
}

const BrokerMirror* LedgerStore::GetBrokerMirror(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _mirrors.find(strategy);
    return it == _mirrors.end() ? nullptr : &it->second;
}

// ── 版本号 ──────────────────────────────────────────────────────

uint64_t LedgerStore::GetVersion(const String& strategy) const {
    std::lock_guard<std::mutex> lock(_mutex);
    auto it = _versions.find(strategy);
    return it == _versions.end() ? 0 : it->second;
}

// ── 持久化 ──────────────────────────────────────────────────────

bool LedgerStore::Save(const String& path) const {
    if (path.empty()) return false;

    std::lock_guard<std::mutex> lock(_mutex);
    nlohmann::json root;
    root["version"] = 1;
    root["initialCapital"] = _initialCapital;
    root["updated_at"] = ToString(time(nullptr));

    nlohmann::json accounts = nlohmann::json::object();
    for (const auto& [name, acc] : _accounts) {
        nlohmann::json item;
        item["allocated"] = acc._allocated;
        item["cash"] = acc._cash;
        item["profit"] = acc._profit;
        item["active"] = acc._active;

        nlohmann::json positions = nlohmann::json::object();
        for (const auto& [symbol, pos] : acc._positions) {
            positions[get_symbol(symbol)] = {
                {"quantity", pos._quantity},
                {"avg_cost", pos._avgPrice},
                {"current_price", pos._currentPrice},
                {"is_margin", pos._isMargin},
                {"margin_ratio", pos._marginRatio},
            };
        }
        item["positions"] = positions;

        nlohmann::json lots = nlohmann::json::object();
        for (const auto& [symbol, batch] : acc._lots) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& lot : batch) {
                arr.push_back({
                    {"quantity", lot._quantity},
                    {"price", lot._price},
                    {"date", static_cast<int64_t>(lot._date)},
                });
            }
            lots[get_symbol(symbol)] = arr;
        }
        item["lots"] = lots;

        item["credit"] = {
            {"borrowedCash", acc._credit.borrowedCash},
            {"borrowedShares", acc._credit.borrowedShares},
            {"shortAvgPrice", acc._credit.shortAvgPrice},
            {"financedAmount", acc._credit.financedAmount},
        };

        accounts[name] = std::move(item);
    }
    root["accounts"] = std::move(accounts);

    try {
        std::filesystem::path p(path);
        if (p.has_parent_path()) {
            std::filesystem::create_directories(p.parent_path());
        }
        std::ofstream ofs(path);
        if (!ofs.is_open()) {
            WARN("[Ledger] Save failed: cannot open {}", path);
            return false;
        }
        ofs << root.dump(2);
        return true;
    } catch (const std::exception& e) {
        WARN("[Ledger] Save failed: {}", e.what());
        return false;
    }
}

bool LedgerStore::Load(const String& path) {
    if (path.empty()) return false;
    if (!std::filesystem::exists(path)) return false;

    try {
        std::ifstream ifs(path);
        if (!ifs.is_open()) return false;
        nlohmann::json root;
        ifs >> root;

        std::lock_guard<std::mutex> lock(_mutex);
        _accounts.clear();
        _versions.clear();
        _initialCapital = root.value("initialCapital", 0.0);

        if (!root.contains("accounts")) return true;
        for (const auto& [name, item] : root["accounts"].items()) {
            Account acc;
            acc._allocated = item.value("allocated", 0.0);
            acc._cash = item.value("cash", 0.0);
            acc._profit = item.value("profit", 0.0);
            acc._active = item.value("active", true);

            if (item.contains("positions")) {
                for (const auto& [sym, pos] : item["positions"].items()) {
                    Position p{};
                    p._symbol = to_symbol(sym);
                    p._quantity = pos.value("quantity", (int64_t)0);
                    p._avgPrice = pos.value("avg_cost", 0.0);
                    p._currentPrice = pos.value("current_price", 0.0);
                    p._isMargin = pos.value("is_margin", false);
                    p._marginRatio = pos.value("margin_ratio", 1.0);
                    acc._positions[p._symbol] = p;
                }
            }

            if (item.contains("lots")) {
                for (const auto& [sym, arr] : item["lots"].items()) {
                    symbol_t symbol = to_symbol(sym);
                    for (const auto& lot : arr) {
                        LedgerLot l{};
                        l._quantity = lot.value("quantity", (int64_t)0);
                        l._price = lot.value("price", 0.0);
                        l._date = static_cast<time_t>(lot.value("date", (int64_t)0));
                        acc._lots[symbol].push_back(l);
                    }
                }
            }

            if (item.contains("credit")) {
                const auto& credit = item["credit"];
                acc._credit.borrowedCash = credit.value("borrowedCash", 0.0);
                if (credit.contains("borrowedShares")) {
                    for (const auto& [sym, qty] : credit["borrowedShares"].items()) {
                        acc._credit.borrowedShares[sym] = qty.get<int64_t>();
                    }
                }
                if (credit.contains("shortAvgPrice")) {
                    for (const auto& [sym, price] : credit["shortAvgPrice"].items()) {
                        acc._credit.shortAvgPrice[sym] = price.get<double>();
                    }
                }
                if (credit.contains("financedAmount")) {
                    for (const auto& [sym, amount] : credit["financedAmount"].items()) {
                        acc._credit.financedAmount[sym] = amount.get<double>();
                    }
                }
            }

            _accounts[name] = std::move(acc);
        }
        return true;
    } catch (const std::exception& e) {
        WARN("[Ledger] Load failed: {}", e.what());
        return false;
    }
}

// ── 从成交流水重建 ──────────────────────────────────────────────

int LedgerStore::RestoreFromFills(const String& strategy, const List<RestoreFill>& fills) {
    std::lock_guard<std::mutex> lock(_mutex);
    Account& acc = _account(strategy);
    acc._positions.clear();
    acc._lots.clear();

    for (const auto& fill : fills) {
        const int64_t delta = fill._quantity;
        if (delta == 0) continue;

        const int64_t oldQty = QuantityOf(acc, fill._symbol);
        const double oldAvg = [&]() {
            auto it = acc._positions.find(fill._symbol);
            return it == acc._positions.end() ? 0.0 : it->second._avgPrice;
        }();

        int64_t closeQty = 0;
        int64_t openQty = 0;
        if (oldQty == 0) {
            openQty = delta;
        } else if ((oldQty > 0) == (delta > 0)) {
            openQty = delta;
        } else {
            closeQty = std::min(std::abs(delta), std::abs(oldQty));
            openQty = std::abs(delta) - closeQty;
        }

        if (closeQty > 0 && oldQty > 0) {
            ConsumeLots(acc._lots, fill._symbol, closeQty);
        }

        {
            auto it = acc._positions.find(fill._symbol);
            if (it == acc._positions.end()) {
                Position pos{};
                pos._symbol = fill._symbol;
                pos._quantity = delta;
                pos._avgPrice = fill._price;
                acc._positions[fill._symbol] = pos;
            } else {
                Position& pos = it->second;
                const int64_t newQty = pos._quantity + delta;
                if (newQty == 0) {
                    acc._positions.erase(it);
                } else {
                    const bool flipped = (pos._quantity > 0) != (newQty > 0);
                    if (flipped) {
                        pos._avgPrice = fill._price;
                    } else if (openQty != 0) {
                        const double total = static_cast<double>(pos._quantity) * pos._avgPrice +
                                             static_cast<double>(delta) * fill._price;
                        pos._avgPrice = total / static_cast<double>(newQty);
                    }
                    pos._quantity = newQty;
                }
            }
        }

        if (fill._isOpen && delta > 0 && (oldQty + delta) > 0) {
            LedgerLot lot{};
            lot._quantity = openQty > 0 ? openQty : delta;
            lot._price = fill._price;
            lot._date = fill._time;
            acc._lots[fill._symbol].push_back(lot);
        }
    }

    _touch(strategy);
    return static_cast<int>(acc._positions.size());
}
