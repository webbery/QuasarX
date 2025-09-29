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

#define REGIST_FEATURE(class_name, json_str) \
    { auto f = FeatureFactory::Create(class_name::name(), nlohmann::json::parse(json_str));\
      _features[f->id()] = f; }

using FeatureFactory = TypeFactory<
    ATRFeature,
    EMAFeature,
    VWAPFeature,
    BasicFeature
>;

unsigned short IFeature::_t = 0;


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
    for (auto node: config._features) {
        // 构建feature id，查找是否已经存在
        auto f = FeatureFactory::Create(node->_type, node->_params);
        auto id = f->id();
        if (_features.count(id) == 0) {
            _features[id] = f;
            for (auto symb : symbols) {
                PipelineInfo& pi = _pipelines[symb];
                pi._gap = config._future;
                pi._features.push_back(f);
                LOG("regist {}", f->desc());
            }
        }
        else {
            delete f;
        }
    }
}

void FeatureSubsystem::InitSecondLvlFeatures() {
    REGIST_FEATURE(VWAPFeature, "{\"N\":1}");
    for (auto& f: _features) {
        for (auto& item: _pipelines) {
            auto& pi = item.second;
            pi._reals.push_back(f.second);
        }
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
        
        List<IFeature*>* pFeats = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mtx);
            auto& pipeinfo = _pipelines[quote._symbol];
            if (pipeinfo._gap != 0 &&
                (_handle->GetRunningMode() == RuningType::Backtest || !_handle->IsOpen(quote._symbol, quote._time))) { // 日间策略:
                send_feature(send_sock, quote, &pipeinfo._features);
                continue;
            } else { // 实时
            }
            pFeats = &pipeinfo._reals;
        }
        
        send_feature(send_sock, quote, pFeats);
    }

    nng_close(send_sock);
    send_sock.id = 0;
    nng_close(recvsock);
    recvsock.id = 0;
}

void FeatureSubsystem::send_feature(nng_socket& s, const QuoteInfo& quote, List<IFeature*>* pFeats) {
    DataFeatures messenger;
    messenger._symbol = quote._symbol;
    Vector<float> features(pFeats->size());
    Vector<size_t> types(pFeats->size());
    int i = 0;
    DEBUG_INFO("{}", quote);
    for (auto& feat: *pFeats) {
        auto val = feat->deal(quote);
        std::visit([&features, i](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, double>) {
                if (!std::isnan(arg)) {
                    features[i] = arg;
                }
            }
            else if constexpr (std::is_same_v<T, Vector<float>>) {
            }
        }, val);
        
        types[i] = feat->id();
        ++i;
    }
    DEBUG_INFO("REAL: {}", features);
    messenger._price = quote._close;
    messenger._data = std::move(features);
    messenger._features = std::move(types);
    // Send to next 
    yas::shared_buffer buf = yas::save<flags>(messenger);
    if (0 != nng_send(s, buf.data.get(), buf.size, 0)) {
        WARN("send features fail.");
    }
}

void FeatureSubsystem::CreateFeature(const String& strategy, const String& name, const nlohmann::json& params) {
    auto f = FeatureFactory::Create(name, params);
    if (!f) {
        LOG("create feature {} fail.", name);
        return;
    }
    auto id = f->id();
    if (_features.count(id) == 0) {
        _features[id] = f;
    }
    else {
        delete f;
        f = _features[id];
    }
    auto& symbols = _tasks[strategy];
    for (auto symbol : symbols) {
        auto& pipeline = _pipelines[symbol];
        pipeline.
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

void FeatureSubsystem::RegistCollection(const String& strategy, const Set<String>& names) {
    auto& symbols = _tasks[strategy];
    for (auto symbol: symbols) {
        auto itr = _pipelines.find(symbol);
        if (itr == _pipelines.end()) {
            WARN("strategy {} not exist", strategy);
            return;
        }
        for (auto& item: names) {
            itr->second._collections[item];
            auto f = FeatureFactory::Create(item, "");
            auto id = f->id();
            if (_features.count(id) == 0) {
                _features[id] = f;
            }
        }
    }
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

Map<symbol_t, Map<String, std::variant<float, List<float>>>> FeatureSubsystem::GetCollection(const String& strategy) {
    Map<symbol_t, Map<String, std::variant<float, List<float>>>> result;
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

const Map<String, std::variant<float, List<float>>>& FeatureSubsystem::GetCollection(symbol_t symbol) const {
    auto itr = _pipelines.find(symbol);
    if (itr == _pipelines.end()) {
        WARN("symbol {}'s feature not exist", symbol);
    }
    return itr->second._collections;
}
