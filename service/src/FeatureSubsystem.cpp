#include "FeatureSubsystem.h"
#include "DataSource.h"
#include "StrategySubSystem.h"
#include "Util/log.h"
#include "Util/system.h"
#include "json.hpp"
#include "server.h"
#include <mutex>
#include <thread>
#include <immintrin.h>
#include "Features/MA.h"
#include "Features/VWAP.h"

using FeatureFactory = TypeFactory<
    ATRFeature,
    EMAFeature,
    VWAPFeature
>;

unsigned short IFeature::_t = 0;

constexpr StringView ATRFeature::name() { return "ATR"; }

const char* ATRFeature::desc() { return "ATR"; }

ATRFeature::ATRFeature(const nlohmann::json& params):_sum(0) {
    try {
        _T = params["N"];
    } catch(const nlohmann::json::exception& e) {
        WARN("ATRFeature exception: {}", e.what());
    }
}

ATRFeature::~ATRFeature() {
    if (_close) delete[] _close;
    if (_tr) delete[] _tr;
}

bool ATRFeature::plug(Server* handle, const String& account) {
    // load data
    auto& position = handle->GetPosition(account);
    auto holds = get_holds(position);
    // _data = handle->PrepareData(holds, DataFrequencyType::Day);
    return true;
}

double ATRFeature::deal(const QuoteInfo& quote) {
    _cnt += 1;
    if (_close == nullptr) {
        _close = new double[_T];
        _close[0] = quote._close;
        _tr = new double[_T]{0};
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
    for (auto mt: {MT_Shanghai, MT_Beijing, MT_Shenzhen}) {
        _working_times[mt] = std::move(GetWorkingRange(mt));
    }
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

    for (auto& node: config._features) {
        // TODO: 构建feature id，查找是否已经存在
        IFeature* f = FeatureFactory::Create(node._type, node._params);
        if (f) {
            _features.push_back(f);
            for (auto symb: symbols) {
                PipelineInfo& pi = _pipelines[symb];
                pi._features.push_back(f);
            }
        } else {
            WARN("{} is not created", node._type);
        }
    }
}

void FeatureSubsystem::run() {
    String port = "inproc://Feature";
    nng_socket send_sock, recvsock;
    if (!Publish(port, send_sock)) {
        WARN("publish {} fail.", port);
        return;
    }
    // 默认接入的都是真实行情
    String source = URI_RAW_QUOTE;
    // if (!is_real_quote) {
    //     source = URI_SIM_QUOTE;
    // }
    if (!Subscribe(source, recvsock)) {
        WARN("subscribe {} fail.", port);
        return;
    }
    SetCurrentThreadName("Feature");
    for (auto& item: _pipelines) {
        for (auto& feat: _features) {
            feat->plug(_handle, "");
        }
    }
    
    // 
    static constexpr std::size_t flags = yas::mem|yas::binary;
    while (!_handle->IsExit()) {
        QuoteInfo quote;
        if (!ReadQuote(recvsock, quote)) {
            continue;
        }
        
        if (_pipelines.count(quote._symbol) == 0 || !is_open(quote._symbol, quote._time))
            continue;
        
        List<IFeature*>* pFeats = nullptr;
        {
            std::unique_lock<std::mutex> lock(_mtx);
            pFeats = &_pipelines[quote._symbol]._features;
        }
        
        DataFeatures messenger;
        messenger._symbols.emplace_back(quote._symbol);
        Vector<float> features(pFeats->size());
        int i = 0;
        for (auto& feat: *pFeats) {
            features[i] = feat->deal(quote);
            messenger._features[i] = ((PrimitiveFeature*)feat)->type();
            ++i;
        }
        messenger._data = std::move(features);
        // Send to next 
        yas::shared_buffer buf = yas::save<flags>(messenger);
        if (0 != nng_send(send_sock, buf.data.get(), buf.size, 0)) {
            WARN("send features fail.");
        }
    }

    nng_close(send_sock);
    send_sock.id = 0;
    nng_close(recvsock);
    recvsock.id = 0;
}

bool FeatureSubsystem::is_open(symbol_t symbol, time_t t) {
    auto exc_type = Server::GetExchange(get_symbol(symbol));
    auto& working = _working_times[exc_type];
    for (auto& tr: working) {
        if (tr == t) {
            return true;
        }
    }
    return false;
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
