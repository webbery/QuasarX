#include "DataContext.h"
#include "Util/system.h"
#include "server.h"
#include "Bridge/SIM/StockHistorySimulation.h"

DataContext::~DataContext() {
    for (auto& item: _signalObservers) {
        delete item;
    }
    for (auto& item: _signals) {
        delete item.second;
    }
}

DataContext::DataContext(const String& strategy, Server* server):_strategy(strategy), _server(server) {
    // 初始化该策略的历史记录
}

// context_t& DataContext::get(const String& name) {
//     return _outputs[name];
// }

// const context_t& DataContext::get(const String& name) const {
//     return _outputs.at(name);
// }

void DataContext::add(const String& name, context_t value) {
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
        else if constexpr (std::is_same_v<T, uint64_t>){
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

// void DataContext::set(const String& name, const context_t& f) {
//     _outputs[name] = f;
// }

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
    for (auto& item: _signals) {
    }
}

void DataContext::RegistSignalObserver(ISignalObserver* obs) {
    _signalObservers.push_back(obs);
}

void DataContext::UnregisterObserver(ISignalObserver* observer) {
    for (auto itr = _signalObservers.begin(); itr != _signalObservers.end(); ++itr) {
        if (*itr == observer) {
            _signalObservers.erase(itr);
            break;
        }
    }
}

void DataContext::ConsumeSignals() {
    cleanupExpiredSignals();
    // TODO: portfolio

    // TODO: risk

    Set<symbol_t> erases;
    for (auto& item: _signals) {
        // signal execution
        for (auto obs: _signalObservers) {
            obs->OnSignalConsume(_strategy, item.second, *this);
        }
        erases.insert(item.first);
    }
    for (auto key: erases) {
        _signals.erase(key);
    }
}

double DataContext::getAvailableCapital() const
{
    auto* exchange = _server->GetAvaliableStockExchange();
    if (exchange) {
        double funds = exchange->GetAvailableFunds(_backtestRunId);
        if (funds > 0) {
            return funds;
        }
    }
    return 0;
}

void DataContext::SetQuote(symbol_t symbol, const QuoteInfo& quote) {
    _quotes[symbol] = quote;
}

const QuoteInfo* DataContext::GetQuote(symbol_t symbol) const {
    auto it = _quotes.find(symbol);
    if (it != _quotes.end()) {
        return &it->second;
    }
    return nullptr;
}

void DataContext::setInitialCapital(double capital) {
    _initialCapital = capital;
}

double DataContext::getInitialCapital() const {
    return _initialCapital;
}

BacktestContext* DataContext::getBacktestContext() {
    if (_backtestRunId == 0) {
        return nullptr;
    }
    auto* exchange = _server->GetAvaliableStockExchange();
    if (!exchange) {
        return nullptr;
    }
    // 转换为 StockHistorySimulation 类型以获取回测上下文
    auto* simExchange = dynamic_cast<StockHistorySimulation*>(exchange);
    if (!simExchange) {
        return nullptr;
    }
    return simExchange->getBacktestContext(_backtestRunId);
}

/**
 * @brief 获取 context_t 的实际类型名称
 * 
 * 使用 std::visit 访问 variant 的实际类型，返回对应的类型名称字符串。
 * 支持的类型：bool, String, uint64_t, double, Vector<float>, Vector<double>,
 *            Vector<uint64_t>, List<symbol_t>, Eigen::MatrixXd
 */
String get_context_type_name(const context_t& ctx) {
    return std::visit([](const auto& v) -> String {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            return "bool";
        } else if constexpr (std::is_same_v<T, String>) {
            return "string";
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return "uint64";
        } else if constexpr (std::is_same_v<T, double>) {
            return "double";
        } else if constexpr (std::is_same_v<T, Vector<float>>) {
            return "vector<float>";
        } else if constexpr (std::is_same_v<T, Vector<double>>) {
            return "vector<double>";
        } else if constexpr (std::is_same_v<T, Vector<uint64_t>>) {
            return "vector<uint64>";
        } else if constexpr (std::is_same_v<T, List<symbol_t>>) {
            return "list<symbol_t>";
        } else if constexpr (std::is_same_v<T, Eigen::MatrixXd>) {
            return "matrix";
        } else {
            return "unknown";
        }
    }, ctx);
}

/**
 * @brief 格式化 context_t 的调试信息
 * 
 * 返回格式：
 * - 标量: "type: value" (如 "double: 123.45")
 * - 向量: "type[size]" (如 "vector<double>[100]")
 * - 字符串: "string: 'value'"
 * - 矩阵: "matrix[rows x cols]"
 */
String format_context_info(const context_t& ctx) {
    return std::visit([](const auto& v) -> String {
        using T = std::decay_t<decltype(v)>;
        
        if constexpr (std::is_same_v<T, bool>) {
            return fmt::format("bool: {}", v);
        } else if constexpr (std::is_same_v<T, String>) {
            return fmt::format("string: '{}'", v.length() > 50 ? v.substr(0, 50) + "..." : v);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            return fmt::format("uint64: {}", v);
        } else if constexpr (std::is_same_v<T, double>) {
            return fmt::format("double: {:.6f}", v);
        } else if constexpr (std::is_same_v<T, Vector<float>>) {
            return fmt::format("vector<float>[{}]", v.size());
        } else if constexpr (std::is_same_v<T, Vector<double>>) {
            return fmt::format("vector<double>[{}]", v.size());
        } else if constexpr (std::is_same_v<T, Vector<uint64_t>>) {
            return fmt::format("vector<uint64>[{}]", v.size());
        } else if constexpr (std::is_same_v<T, List<symbol_t>>) {
            return fmt::format("list<symbol_t>[{}]", v.size());
        } else if constexpr (std::is_same_v<T, Eigen::MatrixXd>) {
            return fmt::format("matrix[{} x {}]", v.rows(), v.cols());
        } else {
            return "unknown";
        }
    }, ctx);
}
