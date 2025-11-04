#include "FeatureSubsystem.h"
#include "DataSource.h"
#include "StrategySubSystem.h"
#include "Util/system.h"
#include "json.hpp"
#include "server.h"
#include <cmath>
#include <cstddef>
#include <mutex>
#include <thread>
#include <immintrin.h>
#include <variant>
#include "Features/MA.h"
#include "Features/VWAP.h"
#include "Features/Basic.h"
#include "Features/ATR.h"

using FeatureFactory = TypeFactory<
    ATRFeature,
    EMAFeature,
    MACDFeature,
    VWAPFeature,
    BasicFeature
>;

bool PrimitiveFeature::isValid(const QuoteInfo& q) {
    if (q._time > _last) {
        _last = q._time;
        return true;
    }
    return false;
}

FeatureSubsystem::FeatureSubsystem(Server* handle): _handle(handle), _thread(nullptr) {
    
}

FeatureSubsystem::~FeatureSubsystem() {
    if (_thread) {
        _thread->join();
        delete _thread;
    }
    
    for (auto& f: _features) {
        delete f.second;
    }
}

void FeatureSubsystem::LoadConfig(const AgentStrategyInfo& config) {
    auto name = config._name;
    Set<symbol_t> symbols;
    for (auto& symbol: config._pool) {
        auto symb = to_symbol(symbol);
        symbols.insert(symb);
    }
    _tasks[name] = symbols;
    LOG("Load Feature {}[{}]", name, symbols);
    // for (auto node: config._features) {
    //     // 构建feature id，查找是否已经存在
    //     auto id = get_feature_id(node->_type, node->_params);
    //     if (_features.count(id) == 0) {
    //         CreateFeature(name, node->_type, node->_params, FeatureKind::LongGap);
    //     }
    // }
}

void FeatureSubsystem::InitSecondLvlFeatures() {
    for (auto& item: _tasks) {
        CreateFeature(item.first, VWAPFeature::name(), nlohmann::json::parse("{\"N\":1}"), FeatureKind::SecondLevel);
    }
}

void FeatureSubsystem::run() {
    nng_socket send_sock, recvsock;
    if (!Publish(URI_FEATURE, send_sock)) {
        WARN("publish {} fail.", URI_FEATURE);
        return;
    }
    // 默认接入的都是真实行情
    String source = URI_RAW_QUOTE;
    // if (!is_real_quote) {
    //     source = URI_SIM_QUOTE;
    // }
    if (!Subscribe(source, recvsock)) {
        WARN("subscribe {} fail.", source);
        return;
    }
    SetCurrentThreadName("Feature");
    // 
    
    while (!_handle->IsExit()) {
        QuoteInfo quote;
        if (!ReadQuote(recvsock, quote)) {
            continue;
        }
        
        {
            std::unique_lock<std::mutex> lock(_mtx);
            if (_pipelines.count(quote._symbol) == 0)
                continue;
        }
        
        {
            std::unique_lock<std::mutex> lock(_mtx);
            auto& pipeinfo = _pipelines[quote._symbol];
            if (pipeinfo._gap != 0 &&
                (_handle->GetRunningMode() == RuningType::Backtest || !_handle->IsOpen(quote._symbol, quote._time))) { // 日间策略:
                send_feature(send_sock, quote, pipeinfo._features);
            } else { // 实时
                send_feature(send_sock, quote, pipeinfo._reals);
            }
            // update external
            UpdateExternalFeature(pipeinfo, quote);
        }
    }

    nng_close(send_sock);
    send_sock.id = 0;
    nng_close(recvsock);
    recvsock.id = 0;
}

void FeatureSubsystem::send_feature(nng_socket& s, const QuoteInfo& quote, const Map<size_t, IFeature*>& pFeats) {
    DataFeatures messenger;
    messenger._symbols.push_back(quote._symbol);
    Vector<feature_t> features(pFeats.size());
    Vector<String> types(pFeats.size());
    int i = 0;
    DEBUG_INFO("{}", quote);
    for (auto& feat: pFeats) {
        feature_t val;
        if (!feat.second->deal(quote, val))
            continue;

        std::visit([&features, i](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, double> || std::is_same_v<T, Vector<double>> || std::is_same_v<T, Eigen::MatrixXd>) {
                features[i] = arg;
            }
        }, val);
        
        types[i] = feat.second->desc();
        ++i;
    }
    DEBUG_INFO("REAL: {}", features);
    // messenger._price = quote._close;
    messenger._data = std::move(features);
    messenger._names = std::move(types);
    // Send to next 
    yas::shared_buffer buf = yas::save<flags>(messenger);
    if (0 != nng_send(s, buf.data.get(), buf.size, 0)) {
        WARN("send features fail.");
    }
}

void FeatureSubsystem::CreateFeature(const String& strategy, const String& name, const nlohmann::json& params, FeatureKind kind) {
    auto id = get_feature_id(name, params);
    IFeature* f = nullptr;
    if (_features.count(id) == 0) {
        f = FeatureFactory::Create(name, params);
        if (!f) {
            LOG("create feature {} fail.", name);
            return;
        }
        f->plug(_handle, "");
        _features[id] = f;
    }
    else {
        f = _features[id];
    }
    
    auto& symbols = _tasks[strategy];
    for (auto symbol : symbols) {
        auto& pipeline = _pipelines[symbol];
        switch (kind) {
        case FeatureKind::LongGap:
            pipeline._features[id] = f;
        break;
        case FeatureKind::SecondLevel:
            pipeline._reals[id] = f;
        break;
        case FeatureKind::Collection:
            pipeline._externals[id] = f;
            pipeline._externalNames[id] = name;
        break;
        default:
        break;
        }
        // pipeline.
    }
    
}

bool FeatureSubsystem::Start() {
    if (!_thread) {
        _thread = new std::thread(&FeatureSubsystem::run, this);
    }

    return true;
}

bool FeatureSubsystem::Start(const String& name, bool is_simulate) {
    auto itr = _tasks.find(name);
    if (itr == _tasks.end())
        return false;

    std::unique_lock<std::mutex> lock(_mtx);
    
    return true;
}

void FeatureSubsystem::Stop() {
    
}

Set<symbol_t> FeatureSubsystem::GetFeatureSymbols() {
    Set<symbol_t> symbs;
    for (auto& item: _pipelines) {
        symbs.insert(item.first);
    }
    return symbs;
}

void FeatureSubsystem::Stop(const String& name) {
    auto itr = _tasks.find(name);
    if (itr == _tasks.end())
        return;

    std::unique_lock<std::mutex> lock(_mtx);

}

void FeatureSubsystem::AddPipeline(const String& name, const List<IFeature*>& feats, const Set<symbol_t>& symbs, bool is_sim) {
    auto itr = _tasks.find(name);
    if (itr == _tasks.end())
        return;
    
    std::unique_lock<std::mutex> lock(_mtx);
    
}

void FeatureSubsystem::ErasePipeline(const String& name) {
    auto& symbols = _tasks[name];
    for (auto symbol: symbols) {
        std::unique_lock<std::mutex> lock(_mtx);
        PipelineInfo& pi = _pipelines[symbol];
        if (!pi._features.empty()) {
            pi._features.clear();
        }
        // IFeature 不删除
        _pipelines.erase(symbol);
    }
    _tasks.erase(name);
}

void FeatureSubsystem::RegistCollection(const String& strategy, const String& featureName, const nlohmann::json& params) {
    CreateFeature(strategy, featureName, params, FeatureKind::Collection);
}

void FeatureSubsystem::ClearCollections(const String& strategy) {
    auto& symbols = _tasks[strategy];
    for (auto symbol: symbols) {
        auto itr = _pipelines.find(symbol);
        if (itr == _pipelines.end()) {
            WARN("strategy {} not exist", strategy);
            return;
        }
        itr->second._collections.clear();
    }
}

Map<symbol_t, Map<String, List<feature_t>>> FeatureSubsystem::GetCollection(const String& strategy) {
    Map<symbol_t, Map<String, List<feature_t>>> result;
    auto& symbols = _tasks[strategy];
    for (auto symbol: symbols) {
        auto itr = _pipelines.find(symbol);
        if (itr == _pipelines.end()) {
            WARN("symbol {} not exist", strategy);
            continue;
        }
        result[symbol] = itr->second._collections;
    }
    return result;
}

const Map<String, List<feature_t>>& FeatureSubsystem::GetCollection(symbol_t symbol) const {
    auto itr = _pipelines.find(symbol);
    if (itr == _pipelines.end()) {
        WARN("symbol {}'s feature not exist", symbol);
    }
    return itr->second._collections;
}

void FeatureSubsystem::UpdateExternalFeature(PipelineInfo& pipeinfo, const QuoteInfo& quote) {
    auto& features = pipeinfo._externals;
    auto& collections = pipeinfo._collections;
    auto& idMap = pipeinfo._externalNames;
    for (auto& item: features) {
        feature_t output;
        item.second->deal(quote, output);
        if (output.valueless_by_exception()) {
            continue;
        }

        auto& name = idMap[item.first];
        collections[name].emplace_back(std::move(output));
    }
}
