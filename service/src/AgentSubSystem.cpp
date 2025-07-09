#include "AgentSubSystem.h"
#include "Transfer.h"
#include "Util/log.h"
#include "Util/system.h"
#include "StrategySubSystem.h"
#include "json.hpp"
#include "nng/nng.h"
#include "std_header.h"
#include "yas/serialize.hpp"
#include "DataSource.h"
#include <filesystem>

XGBoostAgent::XGBoostAgent(const String& path, const nlohmann::json& params):_modelpath(path), _params(params){
    if (!XGBoosterCreate(nullptr, 0, _booster)) {
        FATAL("create booster fail.");
        return;
    }
    if (!path.empty() && std::filesystem::exists(path)) {
        if (!XGBoosterLoadModel(_booster, path.c_str())) {
            FATAL("load model {} fail.", path);
            return;
        }
    }
}

int XGBoostAgent::classify(const Vector<float>& data, short n_samples, short n_features, Vector<float>& result) {
    DMatrixHandle* xgb = nullptr;
    XGDMatrixCreateFromMat(data.data(), n_samples, n_features, 0.0f, xgb);
    bst_ulong out_len;
    const float* out_result;
    XGBoosterPredict(_booster, xgb, 0, 0, 1, &out_len, &out_result);

    // send result to waiter
    result.resize(out_len);
    memcpy(&result[0], out_result, out_len*sizeof(float));
    XGDMatrixFree(xgb);

    return 0;
}

double XGBoostAgent::predict() {
    return 0;
}

void XGBoostAgent::train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) {
    DMatrixHandle* xgb = nullptr;
    XGDMatrixCreateFromMat(data.data(), n_samples, n_features, -1, xgb);
    XGDMatrixSetFloatInfo(xgb, "label", label.data(), label.size());
    // train params
    for (auto& param: _params.items()) {
        String key = param.key();
        String val = param.value();
        XGBoosterSetParam(_booster, key.c_str(), val.c_str());
    }

    for (uint32_t i = 0; i < epoch; ++i) {
        XGBoosterUpdateOneIter(_booster, i, xgb);
    }

    XGDMatrixFree(xgb);
}

XGBoostAgent::~XGBoostAgent() {
    if (_booster) XGBoosterFree(_booster);
}

AgentSubsystem::AgentSubsystem(Server* handle):_handle(handle) {

}

AgentSubsystem::~AgentSubsystem() {
    for (auto& item: _pipelines) {
        if (item.second._transfer) {
            item.second._transfer->stop();
            delete item.second._transfer;
        }
        if (item.second._agent) {
            delete item.second._agent;
        }
        
    }
    _pipelines.clear();
}

void AgentSubsystem::LoadConfig(const AgentStrategyInfo& config) {
    auto& setting = _pipelines[config._name];
    // 约定最终输出端口名为策略名
    for (auto& agent: config._agents) {
        String model_path = agent._modelpath;
        switch(agent._type) {
        case AgentType::XGBoost:
            setting._agent = new XGBoostAgent(model_path, agent._params);
        break;
        default:
        WARN("can not create agent of type: {}", (int)agent._type);
        break;
        }
    }
    
}

void AgentSubsystem::Start() {
    for (auto& item : _pipelines) {
        auto name = item.first;
        auto from = "inproc://Feature";
        auto to = "inpoc://Signal." + name;
        auto agent = item.second._agent;
        item.second._transfer = new Transfer([agent](nng_socket& from, nng_socket& to) {
            constexpr std::size_t flags = yas::mem | yas::binary;
            size_t sz = 0;
            char* buff = nullptr;
            int rv = nng_recv(from, &buff, &sz, NNG_FLAG_ALLOC);
            if (rv != 0) {
                nng_free(buff, sz);
                return true;
            }
            yas::shared_buffer buf;
            buf.assign(buff, sz);
            DataMessenger messenger;
            yas::load<flags>(buf, messenger);
            nng_free(buff, sz);
            Vector<float> result;
            agent->classify(messenger._data, messenger._symbols.size(), messenger._features, result);
            Vector<Signal> signals;
            for (int i = 0; i < result.size(); ++i) {
                Signal sig;
                sig._symbol = messenger._symbols[i];
                if (result[i] > 0.75) {
                    sig._hold = 1;
                }
                else if (result[i] < 0.25) {
                    sig._hold = -1;
                } else {
                    sig._hold = 0;
                }
            }
            // TODO: 保存第N天的操作
            
            buf = yas::save<flags>(signals);
            if (!nng_send(to, buf.data.get(), buf.size, 0)) {
                // 预测结果发送失败,需要根据情况手动操作
                FATAL("send predict result error");
            }
            return true;
        });
        item.second._transfer->start(name, from, to);
    }
}

void AgentSubsystem::Train(const String& strategy) {
    auto& pipeline = _pipelines[strategy];
    pipeline._transfer = new Transfer([agent = pipeline._agent] (nng_socket from, nng_socket to) {
        constexpr std::size_t flags = yas::mem | yas::binary;
        size_t sz = 0;
        char* buff = nullptr;
        int rv = nng_recv(from, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            return true;
        }
        yas::shared_buffer buf;
        buf.assign(buff, sz);
        DataMessenger messenger;
        yas::load<flags>(buf, messenger);
        nng_free(buff, sz);
        return false;
    });
    // pipeline._transfer->start(, , );
}

void AgentSubsystem::Create(const String& strategy, AgentType type, const nlohmann::json& params) {
    if (_pipelines.count(strategy)) {
        return;
    }
    auto& pipeline = _pipelines[strategy];
    switch (type) {
    case AgentType::XGBoost:
        pipeline._agent = new XGBoostAgent(params, "");
    break;
    default:
    break;
    }
}
