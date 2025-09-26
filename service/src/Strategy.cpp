#include "Strategy.h"
#include <algorithm>
#include <cmath>
#include "StrategySubSystem.h"

namespace {
    Map<String, AgentType> agent_types{
        {"XGBOOST", AgentType::XGBoost},
        {"ONNX", AgentType::NeuralNetwork},
        {"LSTM", AgentType::NeuralNetwork},
        {"CNN", AgentType::NeuralNetwork}
    };
}

Set<String> GetAgentTypes() {
    Set<String> types;
    std::for_each(agent_types.begin(), agent_types.end(), [&types](auto&& item) {
        types.insert(item.first);
    });
    return types;
}

AgentStrategyInfo parse_strategy_script(const nlohmann::json& content) {
    AgentStrategyInfo si;
    Set<String> basicTypes{"open", "close", "high", "low", "volume", "turnover"};
    try {
        auto& strategy = content["strategy"];
        // si._name = (String)strategy["name"];
        si._future = (int)strategy["level"];
        std::for_each(strategy["pool"].begin(), strategy["pool"].end(), [&si](auto&& item) {
            si._pool.emplace_back((String)item);
        });
        auto& nodes = content["nodes"];
        Map<String, FeatureNode*> feature_map;
        for (auto& node: nodes) {
            String category = node["category"];
            if (category == "feature" || category == "normal") {
                FeatureNode* fi = new FeatureNode;
                fi->_type = (String)node["type"];
                if (node.contains("params")) {
                    fi->_params = node["params"];
                }
                else if (basicTypes.count(fi->_type)) {
                    fi->_params = fi->_type;
                    fi->_type = BASIC_NAME;
                }
                if (category == "feature") {
                    si._features.emplace_back(fi);
                }
                feature_map[(String)node["id"]] = fi;
            }
            else if (category == "agent") {
                AgentNode ai;
                if (node.contains("class_count")) {
                    ai._classes = (int)node["class_count"];
                } else {
                    ai._classes = 0;
                }
                ai._type = agent_types[node["type"]];
                if (ai._type == AgentType::Unknow) {
                    WARN("unsupport agent type: {}", (String)node["type"]);
                }
                if (node.contains("params")) {
                    ai._params = node["params"];
                }
                ai._modelpath = (String)node["model"];
                si._agents.emplace_back(ai);
            }
            else if (category == "strategy") {
                auto type = (String)node["type"];
                if (type == "interday") {
                    si._strategy = StrategyType::ST_InterDay;
                }
                else if (type == "intraday") {
                    si._strategy = StrategyType::ST_IntraDay;
                }
                else {
                    si._strategy = StrategyType::ST_Unknow;
                }
            }
        }
        auto& edges = content["edges"];
        for (auto& edge: edges) {
            String from = edge["source"];
            String to = edge["target"];
            if (feature_map.count(from) == 0 || feature_map.count(to) == 0)
                continue;
            auto feat = feature_map[from];
            auto next = feature_map[to];
            feat->_nexts.insert(next);
        }
    } catch(const nlohmann::json::exception& e) {
        WARN("parse script fail: {}", e.what());
    }
    return si;
}

feature_t QFeature::Process(const feature_t& input)
{
    return input;
}

feature_t QAgent::Process(const feature_t& input)
{
    return input;
}

QStrategy::QStrategy()
:_isT0(true)
{

}

feature_t QStrategy::Process(const feature_t& input)
{
    return input;
}
