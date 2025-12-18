#include "DataContext.h"
#include "std_header.h"
#include <type_traits>
#include <variant>

DataContext::~DataContext() {}

DataContext::DataContext(const String& strategy, Server* server) {
    // 初始化该策略的历史记录
}

feature_t& DataContext::get(const String& name) {
    return _outputs[name];
}

const feature_t& DataContext::get(const String& name) const {
    return _outputs.at(name);
}

void DataContext::add(const String& name, feature_t value) {
    std::visit([this, &name, &value](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, double> || std::is_same_v<T, uint64_t>) {
            auto itr = _outputs.find(name);
            if (itr == _outputs.end()) {
                _outputs[name] = Vector<T>{std::get<T>(value)};
                } else { [[likely]]
                    std::get<Vector<T>>(itr->second).push_back(std::get<T>(value));
                }
        }
        else {
            set(name, value);
        }
    }, value);
}

bool DataContext::exist(const String& name) {
    return _outputs.contains(name);
}

void DataContext::erase(const String& name) {
    _outputs.erase(name);
}

void DataContext::set(const String& name, const feature_t& f) {
    _outputs[name] = f;
}

void DataContext::SetTime(time_t t) {
    _times.push_back(t);    
}

const List<time_t>& DataContext::GetTime() const {
    return _times;
}

time_t DataContext::Current() {
    if (_times.empty())
        return 0;
    return _times.back();
}

TradeSignal* DataContext::getSignalBySymbol(symbol_t symbol) {
    return _signals[symbol];
}

void DataContext::AddSignal(TradeSignal* signal) {
    _signals[signal->GetSymbol()] = signal;
}

void DataContext::cleanupExpiredSignals() {

}

void DataContext::RegistSignalObserver(ISignalObserver*) {

}

void DataContext::UnregisterObserver(ISignalObserver* observer) {

}

void DataContext::ConsumeSignals() {
    for (auto obs: _observers) {
        for (auto& item: _signals) {
            obs->OnSignalConsume(item.second);
        }
    }
    cleanupExpiredSignals();
}