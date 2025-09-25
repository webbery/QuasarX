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
#include "Agents/XGBoostAgent.h"
#include "Agents/DeepAgent.h"
#include <exception>

AgentSubsystem::AgentSubsystem(Server* handle):_handle(handle) {

}

AgentSubsystem::~AgentSubsystem() {
    for (auto& item : _pipelines) {
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

bool AgentSubsystem::LoadConfig(const AgentStrategyInfo& config) {
    bool status = true;
    auto& setting = _pipelines[config._name];
    if (setting._agent) {
        delete setting._agent;
    }
    if (setting._strategy) {
        delete setting._strategy;
    }
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
        case AgentType::NeuralNetwork:
            setting._agent = NerualNetworkAgentManager::GetInstance().GenerateAgent(model_path, agent._params);
        break;
        case AgentType::LinearRegression:
        break;
        default:
        WARN("can not create agent of type: {}", (int)agent._type);
        break;
        }
    }
    nlohmann::json params;
    switch (config._strategy) {
    case StrategyType::ST_InterDay:
        // setting._strategy = new DailyStrategy(_handle, params);
    break;
    default:
        WARN("no support strategy {}", (int)config._strategy);
    break;
    }
    return status;
}

void AgentSubsystem::Start() {
    auto broker = _handle->GetBrokerSubSystem();
    for (auto& item : _pipelines) {
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

            if (_handle->GetRunningMode() != RuningType::Backtest) {
                if (future > 0 && _handle->IsOpen(messenger._symbol, Now())) {
                    return true;
                }
            }
            
            Vector<float> result;
            try {
                // if (-1 != agent->predict(messenger, result)) {
                //     DEBUG_INFO("predict: {}", result[0]);
                //     int op = strategy->generate(result);
                //     // TODO: 保存第N天的操作
                //     broker->PredictWithDays(messenger._symbol, future, op);
                // }
            } catch (const std::exception& e) {
                FATAL("{}", e.what());
            }
            return true;
            });
        item.second._transfer->start(name.data(), URI_FEATURE);
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
    if (_pipelines.count(strategy)) {
        return;
    }
    // 默认为模拟盘
    auto& pipeline = _pipelines[strategy];
    switch (type) {
    case AgentType::XGBoost:
        // pipeline._agent = new XGBoostAgent(params, "");
    break;
    default:
    break;
    }
}
