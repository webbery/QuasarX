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

void BacktestContext::adjustPosition(symbol_t symbol, int delta, double price) {
    // 调用 StockPositionManager 执行持仓变动，返回费用明细
    TradeFees fees = _positionMgr.AdjustPosition(symbol, delta, price);
    
    // 从 CapitalPool 扣/加资金
    if (_capitalPool && !_strategyNameForCapital.empty()) {
        double amount = std::abs(delta) * price;
        if (delta > 0) {
            // 买入：扣资金 = amount + fees
            _capitalPool->updateAvailable(_strategyNameForCapital, -(amount + fees.total()));
        } else if (delta < 0) {
            // 卖出：加资金 = amount - fees
            _capitalPool->updateAvailable(_strategyNameForCapital, amount - fees.total());
        }
    }
}

double BacktestContext::getCapital() const {
    return _initialCapital;
}

double BacktestContext::getAvailableFunds() const {
    if (_capitalPool && !_strategyNameForCapital.empty()) {
        return _capitalPool->getAvailable(_strategyNameForCapital);
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

            if (fx.cashDelta != 0.0 && _capitalPool && !_strategyNameForCapital.empty()) {
                _capitalPool->updateAvailable(_strategyNameForCapital, fx.cashDelta);
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
