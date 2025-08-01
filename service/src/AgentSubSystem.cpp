#include "AgentSubSystem.h"
#include "server.h"
#include "Transfer.h"
#include "Util/system.h"
#include "StrategySubSystem.h"
#include "json.hpp"
#include "yas/serialize.hpp"
#include "DataSource.h"
#include <filesystem>
#include "BrokerSubSystem.h"
#include "Strategy/Daily.h"

XGBoostAgent::XGBoostAgent(const String& path, int classes, const nlohmann::json& params)
:_modelpath(path), _params(params), _classes(classes) {
    auto code = XGBoosterCreate(nullptr, 0, &_booster);
    if (code) {
        FATAL("create booster fail[{}]: {}", code, XGBGetLastError());
        return;
    }
    if (!path.empty() && std::filesystem::exists(path)) {
        if (XGBoosterLoadModel(_booster, path.c_str())) {
            FATAL("load model {} fail: {}", path, XGBGetLastError());
            return;
        }

        bst_ulong out_len;
        const char ** out_str;
        XGBoosterGetStrFeatureInfo(_booster, "feature_name", &out_len, &out_str);
        if (out_len == 0) {
            FATAL("model feature is zero");
            return;
        }
        for (int i = 0; i < out_len; ++i) {
            const char* attr_name = out_str[i];
            _features.push_back(attr_name);
        }
    }
}

int XGBoostAgent::classify(const DataFeatures& data, short n_samples, Vector<float>& result) {
    if (is_valid(data))
        return -1;

    DMatrixHandle xgb = nullptr;
    if (XGDMatrixCreateFromMat(data._data.data(), n_samples, _features.size(), 0.0f, &xgb)) {
        FATAL("XGDMatrixCreateFromMat fail: {}", XGBGetLastError());
        return -1;
    }
    bst_ulong out_dim;
    const char* config = "{\"type\": 0,\"training\": false,\"iteration_begin\": 0,\"iteration_end\": 0,\"strict_shape\": true}";
    const bst_ulong* out_shape;
    const float* out_result;
    if (XGBoosterPredictFromDMatrix(_booster, xgb, config, &out_shape, &out_dim, &out_result)) {
        FATAL("XGBoosterPredict fail: {}", XGBGetLastError());
        XGDMatrixFree(xgb);
        return -1;
    }

    // send result to waiter
    result.resize(out_dim);
    memcpy(&result[0], out_result, out_dim*sizeof(float));
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
    for (auto& pipeline: _pipelines) {
        for (auto& item: pipeline) {
            if (item.second._transfer) {
                item.second._transfer->stop();
                delete item.second._transfer;
            }
            if (item.second._agent) {
                delete item.second._agent;
            }
            
        }
        pipeline.clear();
    }
}

bool AgentSubsystem::LoadConfig(const AgentStrategyInfo& config) {
    bool status = true;
    Map<String, PipelineInfo>* pipeline = nullptr;
    if (config._virtual) {
        pipeline = &(_pipelines[0]);
    } else {
        pipeline = &(_pipelines[1]);
    }
    auto& setting = (*pipeline)[config._name];
    setting._future = config._future;
    // 约定最终输出端口名为策略名
    for (auto& agent: config._agents) {
        String model_path = String("models/").append(agent._modelpath);
        if (!std::filesystem::exists(model_path)) {
            WARN("model `{}` not exist", model_path);
            status = false;
            continue;
        }
        switch(agent._type) {
        case AgentType::XGBoost:
            setting._agent = new XGBoostAgent(model_path, agent._classes, agent._params);
        break;
        default:
        WARN("can not create agent of type: {}", (int)agent._type);
        break;
        }
    }
    switch (config._strategy) {
    case StrategyType::ST_InterDay:
        setting._strategy = new DailyStrategy(_handle);
    break;
    default:
        WARN("no support strategy {}", (int)config._strategy);
    break;
    }
    return status;
}

void AgentSubsystem::Start() {
    for (int i = 0; i < 2; ++i) {
        auto& pipeline = _pipelines[i];
        auto broker = (i == 0? _handle->GetVirtualSubSystem(): _handle->GetBrokerSubSystem());
        for (auto& item : pipeline) {
            auto name = item.first;
            auto agent = item.second._agent;
            auto future = item.second._future;
            auto strategy = item.second._strategy;
            item.second._transfer = new Transfer([agent, broker, future, strategy, this](nng_socket& from, nng_socket& to) {
                // if (strategy && !strategy->is_valid())
                //     return true;

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
                DataFeatures messenger;
                yas::load<flags>(buf, messenger);
                nng_free(buff, sz);

                if (future > 0 && _handle->IsOpen(messenger._symbol, Now())) {
                    return true;
                }
                
                Vector<float> result;
                if (-1 != agent->classify(messenger, 1, result)) {
                    DEBUG_INFO("predict: {}", result[0]);
                    int op = strategy->generate(result);
                    // TODO: 保存第N天的操作
                    broker->PredictWithDays(messenger._symbol, future, op);
                    // buf = yas::save<flags>(signals);
                    // if (!nng_send(to, buf.data.get(), buf.size, 0)) {
                    //     // 预测结果发送失败,需要根据情况手动操作
                    //     FATAL("send predict result error");
                    // }
                }
                return true;
            });
            item.second._transfer->start(name.data(), URI_FEATURE);
        }
    }
}

void AgentSubsystem::Train(const String& strategy) {
    // auto& pipeline = _pipelines[strategy];
    // pipeline._transfer = new Transfer([agent = pipeline._agent] (nng_socket from, nng_socket to) {
    //     constexpr std::size_t flags = yas::mem | yas::binary;
    //     size_t sz = 0;
    //     char* buff = nullptr;
    //     int rv = nng_recv(from, &buff, &sz, NNG_FLAG_ALLOC);
    //     if (rv != 0) {
    //         nng_free(buff, sz);
    //         return true;
    //     }
    //     yas::shared_buffer buf;
    //     buf.assign(buff, sz);
    //     DataFeatures messenger;
    //     yas::load<flags>(buf, messenger);
    //     nng_free(buff, sz);
    //     return false;
    // });
    // pipeline._transfer->start(, , );
}

void AgentSubsystem::Create(const String& strategy, AgentType type, const nlohmann::json& params) {
    for (auto& pipeline: _pipelines) {
        if (pipeline.count(strategy)) {
            return;
        }
    }
    // 默认为模拟盘
    auto& pipeline = _pipelines[0][strategy];
    switch (type) {
    case AgentType::XGBoost:
        // pipeline._agent = new XGBoostAgent(params, "");
    break;
    default:
    break;
    }
}
