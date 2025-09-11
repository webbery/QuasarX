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

#define REGIST_FEATURE(class_name, json_str) \
    { auto f = FeatureFactory::Create(class_name::name(), nlohmann::json::parse(json_str));\
      _features.push_back(f); }

using FeatureFactory = TypeFactory<
    ATRFeature,
    EMAFeature,
    VWAPFeature
>;

unsigned short IFeature::_t = 0;

constexpr StringView ATRFeature::name() { return "ATR"; }

const char* ATRFeature::desc() { return "ATR"; }

ATRFeature::ATRFeature(const nlohmann::json& params):_sum(0), _close(nullptr) {
    try {
        _T = params["N"];
        _tr.resize(_T);
    } catch(const nlohmann::json::exception& e) {
        WARN("ATRFeature exception: {}", e.what());
    }
}

size_t ATRFeature::id() {
    return std::hash<StringView>()(name());
}

ATRFeature::~ATRFeature() {
    if (_close) delete[] _close;
}

bool ATRFeature::plug(Server* handle, const String& account) {
    // load data
    auto& position = handle->GetPosition(account);
    auto holds = get_holds(position);
    // _data = handle->PrepareData(holds, DataFrequencyType::Day);
    return true;
}

feature_t ATRFeature::deal(const QuoteInfo& quote, double extra) {
    _cnt += 1;
    if (_close == nullptr) {
        _close = new double[_T];
        _close[0] = quote._close;
        return nan("");
    }
    double prev_close = _close[_cur];
    
    double abs1 = quote._high - quote._low;
    double abs2 = fabs(prev_close - quote._high);
    double abs3 = fabs(prev_close - quote._low);
    double tr = std::max(abs3, std::max(abs1, abs2));
    _tr[_cur] = tr;

    _cur = (_cur + 1) % _T;
    _close[_cur] = quote._close;

    _sum += tr;
    if (_cnt >= _T + 1) {
        _sum -= _close[(_cur - 1) % _T];
        return _sum / _T;
    }
    return nan("");
}

FeatureSubsystem::FeatureSubsystem(Server* handle): _handle(handle), _thread(nullptr) {
    
}

FeatureSubsystem::~FeatureSubsystem() {
    if (_thread) {
        _thread->join();
        delete _thread;
    }
    
    for (auto f: _features) {
        delete f;
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

    for (auto node: config._features) {
        // TODO: 构建feature id，查找是否已经存在
        auto block = GenerateBlock(node);
        if (block) {
            for (auto symb: symbols) {
                PipelineInfo& pi = _pipelines[symb];
                pi._gap = config._future;
                pi._features.push_back(block);
            }
        }
    }
}

FeatureSubsystem::FeatureBlock* FeatureSubsystem::GenerateBlock(FeatureNode* node) {
    auto f = FeatureFactory::Create(node->_type, node->_params);
    if (!f)
        return nullptr;

    f->plug(_handle, "");
    auto block = new FeatureBlock;
    block->_feature = f;
    if (!node->_nexts.empty()) {
        for (auto next: node->_nexts) {
            auto next_block = GenerateBlock(next);
            if (!next_block) {
                WARN("create feature {} fail", next->_type);
                continue;
            }
            block->_nexts.insert(next_block);
        }
    }
    return block;
}

void FeatureSubsystem::InitSecondLvlFeatures() {
    REGIST_FEATURE(VWAPFeature, "{\"N\":1}");
    for (auto f: _features) {
        for (auto& item: _pipelines) {
            auto& pi = item.second;
            pi._reals.push_back(f);
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
            if (pipeinfo._gap != 0 && !_handle->IsOpen(quote._symbol, quote._time)) { // 日间策略:
                send_feature(send_sock, quote, pipeinfo._features);
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

void FeatureSubsystem::send_feature(nng_socket& s, const QuoteInfo& quote, const List<FeatureBlock*>& pFeats) {
    DataFeatures messenger;
    messenger._symbol = quote._symbol;
    Vector<float> features(pFeats.size());
    Vector<size_t> types(pFeats.size());
    int i = 0;
    for (auto& block: pFeats) {
        auto val = block->_feature->deal(quote);
        types[i] = block->_feature->id();
        bool ret = std::visit([this, &features, i, block, &quote](auto&& arg)->bool {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, double>) {
                if (std::isnan(arg)) {
                    return false;
                }
                if (!block->_nexts.empty()) {
                    double dv = 0;
                    for (auto itr = block->_nexts.begin(); itr != block->_nexts.end(); ++itr) {
                        dv += recursive_feature(*itr, quote, arg);
                    }
                    arg += dv;
                }
                features[i] = arg;
                return true;
            }
            else if constexpr (std::is_same_v<T, Vector<float>>) {
                return true;
            }
        }, val);
        if (!ret) {
            continue;
        }
        
        ++i;
    }
    DEBUG_INFO("{}", features);
    messenger._price = quote._close;
    messenger._data = std::move(features);
    messenger._features = std::move(types);
    // Send to next 
    yas::shared_buffer buf = yas::save<flags>(messenger);
    if (0 != nng_send(s, buf.data.get(), buf.size, 0)) {
        WARN("send features fail.");
    }
}

double FeatureSubsystem::recursive_feature(FeatureBlock* block, const QuoteInfo& quote, double cur) {
    auto val = block->_feature->deal(quote, cur);
    for (auto next: block->_nexts) {
        double dv = 0;
        for (auto itr = block->_nexts.begin(); itr != block->_nexts.end(); ++itr) {
            dv += recursive_feature(*itr, quote, std::get<double>(val));
        }
        std::get<double>(val) += dv;
    }
    return std::get<double>(val);
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

void FeatureSubsystem::DeleteBlock(FeatureBlock* block) {
    if (block->_nexts.empty()) {
        delete block;
    } else {
        for (auto ptr: block->_nexts) {
            DeleteBlock(ptr);
        }
        block->_nexts.clear();
    }
}

void FeatureSubsystem::ErasePipeline(const String& name) {
    auto& symbols = _tasks[name];
    for (auto symbol: symbols) {
        std::unique_lock<std::mutex> lock(_mtx);
        PipelineInfo& pi = _pipelines[symbol];
        if (!pi._features.empty()) {
            for (auto ptr: pi._features) {
                DeleteBlock(ptr);
            }
            pi._features.clear();
        }
        // IFeature 不删除
        _pipelines.erase(symbol);
    }
    _tasks.erase(name);
}

