#include "Bridge/SIM/BacktestContext.h"
#include <atomic>

BacktestContext::BacktestContext(run_id_t run_id, const String& strategy_name)
    : _runId(run_id)
    , _strategy_name(strategy_name)
    , _availableFunds(100000.0)
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
        std::lock_guard<std::mutex> lock(_positionMtx);
        for (auto& item : _positions) {
            delete item.second;
        }
        _positions.clear();
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
    std::lock_guard<std::mutex> lock(_positionMtx);
    auto itr = _positions.find(symbol);
    if (itr == _positions.end()) {
        return 0;
    }
    return itr->second->load(std::memory_order_relaxed);
}

void BacktestContext::setPosition(symbol_t symbol, int64_t qty) {
    std::lock_guard<std::mutex> lock(_positionMtx);
    auto itr = _positions.find(symbol);
    if (itr == _positions.end()) {
        auto* atomicPos = new std::atomic<int64_t>(qty);
        _positions[symbol] = atomicPos;
    } else {
        itr->second->store(qty, std::memory_order_relaxed);
    }
}

void BacktestContext::adjustPosition(symbol_t symbol, int delta) {
    std::lock_guard<std::mutex> lock(_positionMtx);
    auto itr = _positions.find(symbol);
    if (itr == _positions.end()) {
        auto* atomicPos = new std::atomic<int64_t>(delta);
        _positions[symbol] = atomicPos;
    } else {
        itr->second->fetch_add(delta, std::memory_order_relaxed);
    }
}

void BacktestContext::setCapital(double capital) {
    _capital = capital;
    _availableFunds.store(capital, std::memory_order_relaxed);
}

double BacktestContext::getAvailableFunds() const {
    return _availableFunds.load(std::memory_order_relaxed);
}

void BacktestContext::setAvailableFunds(double funds) {
    _availableFunds.store(funds, std::memory_order_relaxed);
}

bool BacktestContext::tryReserveFunds(double amount) {
    double current = _availableFunds.load(std::memory_order_relaxed);
    double expected = current;

    while (true) {
        if (expected < amount) {
            return false;  // 资金不足
        }
        if (_availableFunds.compare_exchange_strong(expected, expected - amount,
                std::memory_order_release, std::memory_order_relaxed)) {
            return true;
        }
        // expected 已被更新为当前值，继续循环重试
    }
}

void BacktestContext::releaseFunds(double amount) {
    _availableFunds.fetch_add(amount, std::memory_order_release);
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

