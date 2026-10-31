#include "Bridge/SIM/BacktestContext.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include <atomic>

BacktestContext::BacktestContext(run_id_t run_id, const String& strategy_name)
    : _runId(run_id)
    , _strategy_name(strategy_name)
{
}

BacktestContext::~BacktestContext() {
    // 清理原子指针
    {
        std::lock_guard<std::mutex> lock(_indexMtx);
        for (auto& item : _curIndices) {
            delete item.second;
        }
        _curIndices.clear();
    }
    {
        std::lock_guard<std::mutex> lock(_orderQueueMtx);
        for (auto& item : _orderQueues) {
            delete item.second;
        }
        _orderQueues.clear();
    }
    // 清理 OrderContext（回测模式由 BacktestContext 持有所有权）
    {
        std::lock_guard<std::mutex> lock(_orderReportMtx);
        for (auto& item : _orderReports) {
            delete item.second;
        }
        _orderReports.clear();
    }
    // 归还本上下文建立的账本注册（长生命周期策略的注册不属于本上下文）
    if (_capitalReserved && _ledger) {
        double reclaimed = _ledger->Reclaim(_strategyNameForCapital);
        INFO("[BacktestContext] Reclaimed {:.0f} capital for strategy '{}' (runId={})",
             reclaimed, _strategyNameForCapital, _runId);
    }
}

uint32_t BacktestContext::getCurIndex(symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_indexMtx);
    auto itr = _curIndices.find(symbol);
    if (itr == _curIndices.end()) {
        return 0;
    }
    return itr->second->load(std::memory_order_relaxed);
}

void BacktestContext::setCurIndex(symbol_t symbol, uint32_t index) {
    std::lock_guard<std::mutex> lock(_indexMtx);
    auto itr = _curIndices.find(symbol);
    if (itr == _curIndices.end()) {
        auto* atomicIndex = new std::atomic<uint32_t>(index);
        _curIndices[symbol] = atomicIndex;
    } else {
        itr->second->store(index, std::memory_order_relaxed);
    }
}

uint32_t BacktestContext::incrementCurIndex(symbol_t symbol) {
    std::lock_guard<std::mutex> lock(_indexMtx);
    auto itr = _curIndices.find(symbol);
    if (itr == _curIndices.end()) {
        auto* atomicIndex = new std::atomic<uint32_t>(1);
        _curIndices[symbol] = atomicIndex;
        return 1;
    } else {
        return itr->second->fetch_add(1, std::memory_order_relaxed) + 1;
    }
}

int64_t BacktestContext::getPosition(symbol_t symbol) const {
    return _positionMgr.GetPosition(symbol);
}

void BacktestContext::setPosition(symbol_t symbol, int64_t qty) {
    _positionMgr.SetPosition(symbol, qty);
}

void BacktestContext::adjustPosition(symbol_t symbol, int delta, double price,
                                     CreditAction credit, double marginRatio) {
    // 只有显式的融券卖出（ShortSell）才允许在无多仓时开出负仓。
    // 普通卖单（credit=None）在无持仓时仍然被拒绝 —— 现有多头回测行为不变。
    bool allowShort = (credit == CreditAction::ShortSell);

    // 调用 StockPositionManager 执行持仓变动，返回费用明细
    TradeFees fees = _positionMgr.AdjustPosition(symbol, delta, price, allowShort);

    // 从账本扣/加资金
    if (_ledger && !_strategyNameForCapital.empty()) {
        double amount = std::abs(delta) * price;
        if (delta > 0) {
            // 买入：扣资金 = amount + fees
            // 融资买入只占用保证金部分，其余为借入现金（不占用可用资金）
            double cashOut = (credit == CreditAction::MarginBuy)
                                 ? amount * std::clamp(marginRatio, 0.0, 1.0)
                                 : amount;
            _ledger->UpdateAvailable(_strategyNameForCapital, -(cashOut + fees.total()));
        } else if (delta < 0) {
            // 卖出：加资金 = amount - fees
            _ledger->UpdateAvailable(_strategyNameForCapital, amount - fees.total());
        }
    }

    // 信用敞口回写（融资余额 / 在借股数 / 开空均价）
    if (credit != CreditAction::None) {
        applyCreditFill(get_symbol(symbol), credit, std::abs(delta), price, marginRatio);
    }
}

void BacktestContext::applyCreditFill(const String& symbol, CreditAction action,
                                      int64_t quantity, double price, double marginRatio) {
    if (action == CreditAction::None || quantity <= 0 || price <= 0.0) {
        return;
    }

    switch (action) {
        case CreditAction::MarginBuy: {
            // 融资买入：成交额按保证金比例拆成自有资金与借入现金
            double gross = static_cast<double>(quantity) * price;
            double borrowed = gross * (1.0 - std::clamp(marginRatio, 0.0, 1.0));
            _creditState.borrowedCash += borrowed;
            _creditState.financedAmount[symbol] += borrowed;
            break;
        }
        case CreditAction::ShortSell: {
            // 融券卖出：在借股数增加，开空均价按加权更新
            auto& shares = _creditState.borrowedShares[symbol];
            auto& avg = _creditState.shortAvgPrice[symbol];
            double oldValue = static_cast<double>(shares) * avg;
            shares += quantity;
            avg = (oldValue + static_cast<double>(quantity) * price) / static_cast<double>(shares);
            break;
        }
        case CreditAction::ShortCover: {
            // 买入平空：归还借券，在借股数减少
            auto itShares = _creditState.borrowedShares.find(symbol);
            if (itShares == _creditState.borrowedShares.end()) {
                break;
            }
            itShares->second -= quantity;
            if (itShares->second <= 0) {
                _creditState.borrowedShares.erase(itShares);
                _creditState.shortAvgPrice.erase(symbol);
            }
            break;
        }
        case CreditAction::None:
            break;
    }
}

double BacktestContext::getCapital() const {
    return _initialCapital;
}

double BacktestContext::getAvailableFunds() const {
    if (_ledger && !_strategyNameForCapital.empty()) {
        return _ledger->GetAvailable(_strategyNameForCapital);
    }
    return 0.0;
}

void BacktestContext::setCapital(double capital) {
    _initialCapital = capital;
}

QuoteInfo* BacktestContext::getQuote(symbol_t symbol) {
    std::lock_guard<std::mutex> lock(_quoteMtx);
    auto itr = _quotes.find(symbol);
    if (itr == _quotes.end()) {
        return nullptr;
    }
    return &itr->second;
}

const QuoteInfo* BacktestContext::getQuote(symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(_quoteMtx);
    auto itr = _quotes.find(symbol);
    if (itr == _quotes.end()) {
        return nullptr;
    }
    return &itr->second;
}

void BacktestContext::setQuote(symbol_t symbol, const QuoteInfo& quote) {
    std::lock_guard<std::mutex> lock(_quoteMtx);
    _quotes[symbol] = quote;
}

boost::lockfree::queue<OrderInfo>* BacktestContext::getOrCreateOrderQueue(symbol_t symbol) {
    std::lock_guard<std::mutex> lock(_orderQueueMtx);
    auto itr = _orderQueues.find(symbol);
    if (itr == _orderQueues.end()) {
        auto* queue = new boost::lockfree::queue<OrderInfo>(MAX_ORDER_PER_SECOND);
        _orderQueues[symbol] = queue;
        return queue;
    }
    return itr->second;
}

boost::lockfree::queue<OrderInfo>* BacktestContext::getOrderQueue(symbol_t symbol) {
    std::lock_guard<std::mutex> lock(_orderQueueMtx);
    auto itr = _orderQueues.find(symbol);
    if (itr == _orderQueues.end()) {
        return nullptr;
    }
    return itr->second;
}

void BacktestContext::addOrderReport(size_t order_id, OrderContext* ctx) {
    std::lock_guard<std::mutex> lock(_orderReportMtx);
    _orderReports[order_id] = ctx;
}

OrderContext* BacktestContext::getOrderReport(size_t order_id) {
    std::lock_guard<std::mutex> lock(_orderReportMtx);
    auto itr = _orderReports.find(order_id);
    if (itr == _orderReports.end()) {
        return nullptr;
    }
    return itr->second;
}

double BacktestContext::getProgress() const {
    if (_totalBars == 0) {
        return 0.0;
    }

    // 计算所有标的的平均进度
    std::lock_guard<std::mutex> lock(_indexMtx);
    if (_curIndices.empty()) {
        return 0.0;
    }

    double totalProgress = 0.0;
    for (const auto& item : _curIndices) {
        totalProgress += item.second->load(std::memory_order_relaxed);
    }

    double avgProgress = totalProgress / _curIndices.size();
    return std::min(1.0, avgProgress / _totalBars);
}

void BacktestContext::addSymbol(symbol_t symbol) {
    std::lock_guard<std::mutex> lock(_symbolsMtx);
    _symbols.insert(symbol);
}

void BacktestContext::processPositionEvents(time_t barDate, double currentPrice) {
    for (auto& [symbol, events] : _positionEvents) {
        size_t& nextIdx = _nextEventIdx[symbol];

        while (nextIdx < events.size() && events[nextIdx]->triggerDate() <= barDate) {
            const auto& event = events[nextIdx];

            PositionEffect fx = _positionMgr.ApplyEvent(symbol, *event, currentPrice);

            if (fx.cashDelta != 0.0 && _ledger && !_strategyNameForCapital.empty()) {
                _ledger->UpdateAvailable(_strategyNameForCapital, fx.cashDelta);
            }

            if (fx.qtyDelta != 0 || fx.cashDelta != 0) {
                INFO("[BacktestContext] Event applied: symbol={}, date={}, "
                     "qtyDelta={}, cashDelta={}",
                     get_symbol(symbol), ToString(event->triggerDate(), "%Y-%m-%d"),
                     fx.qtyDelta, fx.cashDelta);
            }

            ++nextIdx;
        }
    }
}
