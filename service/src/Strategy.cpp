#include "Strategy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <unordered_map>
#include "Bridge/SIM/SIMExchange.h"
#include "Bridge/exchange.h"
#include "StrategyNode.h"
#include "StrategySubSystem.h"
#include "BrokerSubSystem.h"
#include "Util/log.h"
#include "server.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/FunctionNode.h"
#include "Nodes/SignalNode.h"
#include "boost/algorithm/string.hpp"

namespace {
    Map<String, SignalGeneratorType> agent_types{
        {"XGBOOST", SignalGeneratorType::XGBoost},
        {"ONNX", SignalGeneratorType::NeuralNetwork},
        {"LSTM", SignalGeneratorType::NeuralNetwork},
        {"CNN", SignalGeneratorType::NeuralNetwork},
        {"FTMIX", SignalGeneratorType::FeatureMixed}
    };

    Map<String, StrategyNodeType> node_type_map{
        {"input", StrategyNodeType::Input},
        {"output", StrategyNodeType::Output},
        {"operation", StrategyNodeType::Operation},
        {"function", StrategyNodeType::Function},
        {"feature", StrategyNodeType::Feature},
        {"signal", StrategyNodeType::Signal},
    };

    Map<String, StatisticIndicator> statistics{
        {"sharp", StatisticIndicator::Sharp},
        {"VAR", StatisticIndicator::VaR},
        {"ES", StatisticIndicator::ES},
        {"winRate", StatisticIndicator::WinRate},
        {"annualReturn", StatisticIndicator::AnualReturn},
        {"totalReturn", StatisticIndicator::TotalReturn},
        {"maxDrawdown", StatisticIndicator::MaxDrawDown},
        {"CalmarRatio", StatisticIndicator::Calmar}
    };
}

Set<String> GetAgentTypes() {
    Set<String> types;
    std::for_each(agent_types.begin(), agent_types.end(), [&types](auto&& item) {
        types.insert(item.first);
    });
    return types;
}

// AgentStrategyInfo parse_strategy_script(const nlohmann::json& content) {
//     AgentStrategyInfo si;
//     Set<String> basicTypes{"open", "close", "high", "low", "volume", "turnover"};
//     try {
//         auto& strategy = content["strategy"];
//         // si._name = (String)strategy["name"];
//         si._future = (int)strategy["level"];
//         std::for_each(strategy["pool"].begin(), strategy["pool"].end(), [&si](auto&& item) {
//             si._pool.emplace_back((String)item);
//         });
//         auto& nodes = content["nodes"];
//         Map<String, FeatureNode*> feature_map;
//         for (auto& node: nodes) {
//             String category = node["category"];
//             if (category == "feature" || category == "normal") {
//                 FeatureNode* fi = new FeatureNode;
//                 fi->_type = (String)node["type"];
//                 if (node.contains("params")) {
//                     fi->_params = node["params"];
//                 }
//                 else if (basicTypes.count(fi->_type)) {
//                     fi->_params = fi->_type;
//                     fi->_type = BASIC_NAME;
//                 }
//                 if (category == "feature") {
//                     si._features.emplace_back(fi);
//                 }
//                 feature_map[(String)node["id"]] = fi;
//             }
//             else if (category == "agent") {
//                 AgentNode ai;
//                 if (node.contains("class_count")) {
//                     ai._classes = (int)node["class_count"];
//                 } else {
//                     ai._classes = 0;
//                 }
//                 ai._type = agent_types[node["type"]];
//                 if (ai._type == SignalGeneratorType::Unknow) {
//                     WARN("unsupport agent type: {}", (String)node["type"]);
//                 }
//                 if (node.contains("params")) {
//                     ai._params = node["params"];
//                 }
//                 ai._modelpath = (String)node["model"];
//                 si._agents.emplace_back(ai);
//             }
//             else if (category == "strategy") {
//                 auto type = (String)node["type"];
//                 if (type == "interday") {
//                     si._strategy = StrategyType::ST_InterDay;
//                 }
//                 else if (type == "intraday") {
//                     si._strategy = StrategyType::ST_IntraDay;
//                 }
//                 else {
//                     si._strategy = StrategyType::ST_Unknow;
//                 }
//             }
//         }
//         auto& edges = content["edges"];
//         for (auto& edge: edges) {
//             String from = edge["source"];
//             String to = edge["target"];
//             if (feature_map.count(from) == 0 || feature_map.count(to) == 0)
//                 continue;
//             auto feat = feature_map[from];
//             auto next = feature_map[to];
//             feat->_nexts.insert(next);
//         }
//     } catch(const nlohmann::json::exception& e) {
//         WARN("parse script fail: {}", e.what());
//     }
//     return si;
// }

QNode* generate_input_node(const String& id, Server* server) {
    auto node = new QuoteInputNode(server);
    node->setID(atoi(id.c_str()));
    return node;
}

QNode* generate_operation_node(const String& id, Server* server) {
    auto node = new OperationNode;
    node->setID(atoi(id.c_str()));
    // String operatorName = data["params"]["method"]["value"];
    // if (!node->parseFomula(lines)) {
    //     WARN("parse {} formula fail.", id);
    //     delete node;
    //     return nullptr;
    // }
    return node;
}

QNode* generate_function_node(const String& id, Server* server) {
    auto node = new FunctionNode(server);
    node->setID(atoi(id.c_str()));
    return node;
}

QNode* generate_signal_node(const String& strategyName, const String& id, Server* server) {
    auto node = new SignalNode(server);
    node->setID(atoi(id.c_str()));

    auto brokerSystem = server->GetBrokerSubSystem();
    brokerSystem->CleanAllIndicators(strategyName);
    for (auto& item: statistics) {
        brokerSystem->RegistIndicator(strategyName, item.second);
    }
    return node;
}

List<QNode*> parse_strategy_script_v2(const nlohmann::json& content, Server* server) {
    List<QNode*> graph;
    auto& nodes = content["nodes"];
    auto& edges = content["edges"];
    String strategyName = content["id"];
    Map<uint32_t, QNode*> nodeMap;
    Map<uint32_t, nlohmann::json> nodeConfigMap;
    for (auto& node: nodes) {
        String node_type = node["data"]["nodeType"];
        QNode* nodeInstance = nullptr;
        auto type = node_type_map[node_type];
        switch (type) {
        case StrategyNodeType::Input: 
            nodeInstance = generate_input_node(node["id"], server);
            break;
        case StrategyNodeType::Feature:
            break;
        case StrategyNodeType::Operation:
            nodeInstance = generate_operation_node(node["id"], server);
            break;
        case StrategyNodeType::Function:
            nodeInstance = generate_function_node(node["id"],  server);
            break;
        case StrategyNodeType::Signal:
            nodeInstance = generate_signal_node(strategyName, node["id"], server);
        default:
            break;
        }
        if (!nodeInstance) {
            continue;
        }
        nodeConfigMap[nodeInstance->id()] = std::move(node["data"]);
        nodeMap[nodeInstance->id()] = nodeInstance;
        graph.push_back(nodeInstance);
    }
    // 构建连接关系
    for (auto& edge: edges) {
        String from = edge["source"];
        String target = edge["target"];
        String sourceHandle = edge["sourceHandle"];
        String targetHandle = edge["targetHandle"];
        
        auto itr = nodeMap.find(atoi(from.c_str()));
        if (itr == nodeMap.end()) {
            WARN("node {} not exist", from);
            continue;
        }
        auto next_itr = nodeMap.find(atoi(target.c_str()));
        if (next_itr == nodeMap.end()) {
            continue;
        }
        if (sourceHandle == "output") {
            sourceHandle = from;
        } else {
            boost::algorithm::replace_all(sourceHandle, "field", from);
        }
        if (targetHandle == "input") {
            targetHandle = target;
        } else {
            boost::algorithm::replace_all(targetHandle, "field", target);
        }
        itr->second->Connect(next_itr->second, sourceHandle, targetHandle);
    }
    // 初始化
    for (auto& node: nodeMap) {
        auto& config = nodeConfigMap[node.first];
        node.second->Init(config);
    }
    return graph;
}

List<QNode*> topo_sort(const List<QNode*>& graph) {
    std::unordered_map<QNode*, int> inDegree;
    List<QNode*> nodeQueue;
    
    for (auto pNode: graph) {
        inDegree[pNode] = pNode->in_degree();
        if (pNode->in_degree() == 0) {
            nodeQueue.push_back(pNode);
        }
    }

    List<QNode*> sorted_nodes;
    while (!nodeQueue.empty()) {
        auto front = nodeQueue.front();
        nodeQueue.pop_front();
        sorted_nodes.push_back(front);

        auto& outs = front->outs();
        for (auto next: outs) {
            --inDegree[next.second];
            if (inDegree[next.second] == 0) {
                nodeQueue.push_back(next.second);
            }
        }
    }
    
    if (sorted_nodes.size() != graph.size()) {
        WARN("Ring exist, build flow error.");
        throw std::logic_error("Ring exist, build flow error.");
    }
    return sorted_nodes;
}
