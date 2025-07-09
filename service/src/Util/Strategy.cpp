#include "Util/Strategy.h"
#include "Util/log.h"
#include <cmath>

AgentStrategyInfo parse_strategy_script(const nlohmann::json& content) {
    AgentStrategyInfo si;
    static Map<String, AgentType> agent_types{
        {"XGBOOST", AgentType::XGBoost}
    };
    try {
        auto& strategy = content["strategy"];
        si._name = (String)strategy["name"];
        si._level = (int)strategy["level"];
        std::for_each(strategy["pool"].begin(), strategy["pool"].end(), [&si](auto&& item) {
            si._pool.emplace_back((String)item);
        });
        auto& nodes = content["nodes"];
        for (auto& node: nodes) {
            String category = node["category"];
            if (category == "feature") {
                FeatureInfo fi;
                fi._type = (String)node["type"];
                if (node.contains("params")) {
                    fi._params = node["params"];
                }
                si._features.emplace_back(std::move(fi));
            }
            else if (category == "agent") {
                AgentInfo ai;
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
        }
        auto& edges = content["edges"];
        for (auto& edge: edges) {
            // if ()
        }
    } catch(const nlohmann::json::exception& e) {
        WARN("parse script fail: {}", e.what());
    }
    return si;
}