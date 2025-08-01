#include "Risk/StopLoss.h"
#include <mutex>

void SLPercentage::check(const Map<symbol_t, QuoteInfo>& infos, List<symbol_t>&& sells) {
    std::unique_lock<std::mutex> lock(_mutex);
    for (auto& item: infos) {
        auto itr = _org_price_map.find(item.first);
        if (itr == _org_price_map.end())
            continue;

        double org_price = itr->second.first;
        double percent = itr->second.second;
        if (item.second._close < org_price * (1 - percent)) {
            sells.emplace_back(item.first);
        }
    }
}

void SLPercentage::add(const symbol_t& symbol, const StopLossInfo& info) {
    std::unique_lock<std::mutex> lock(_mutex);
    _org_price_map[symbol].first = info._price;
    _org_price_map[symbol].second = info._percent;
}
void SLPercentage::del(const List<symbol_t>& symbols) {
    std::unique_lock<std::mutex> lock(_mutex);
    for (auto& symbol: symbols) {
        _org_price_map.erase(symbol);
    }
}

void SLPercentage::update(const symbol_t& symbol, const StopLossInfo& info) {
    std::unique_lock<std::mutex> lock(_mutex);
    _org_price_map[symbol].first = info._price;
    _org_price_map[symbol].second = info._percent;
}

void StepPercentage::check(const Map<symbol_t, QuoteInfo>& infos, List<symbol_t>&& sells) {
    std::unique_lock<std::mutex> lock(_mutex);
    for (auto& item: infos) {
        auto itr = _price_map.find(item.first);
        if (itr == _price_map.end())
            continue;

        double cur_price = item.second._close;
        if (cur_price > itr->second._lower) {
            itr->second._lower = cur_price;
        }
        double step = itr->second._org_price * itr->second._percent;
        if (item.second._close < (itr->second._lower - step)) {
            sells.emplace_back(item.first);
        }
    }
}

void StepPercentage::add(const symbol_t& symbol, const StopLossInfo& info) {
    std::unique_lock<std::mutex> lock(_mutex);
    _price_map[symbol]._org_price = info._price;
    _price_map[symbol]._lower = info._price;
    _price_map[symbol]._percent = info._percent;
}
void StepPercentage::del(const List<symbol_t>& symbols) {
    std::unique_lock<std::mutex> lock(_mutex);
    for (auto& symbol: symbols) {
        _price_map.erase(symbol);
    }
}
void StepPercentage::update(const symbol_t& symbol, const StopLossInfo& info) {
    std::unique_lock<std::mutex> lock(_mutex);
    _price_map[symbol]._org_price = info._price;
    _price_map[symbol]._lower = info._price;
    _price_map[symbol]._percent = info._percent;
}

void ATR::check(const Map<symbol_t, QuoteInfo>&, List<symbol_t>&& sells) {
    
}

void ATR::add(const symbol_t& symbol, const StopLossInfo& info) {

}
void ATR::del(const List<symbol_t>& symbol) {

}
void ATR::update(const symbol_t& symbol, const StopLossInfo& info) {
    
}