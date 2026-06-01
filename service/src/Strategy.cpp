#include "Strategy.h"
#include <algorithm>
#include <cmath>
#include <exception>
#include <stdexcept>
#include <unordered_map>
#include "Nodes/TestNode.h"
#include "Nodes/ExecuteNode.h"
#include "Nodes/QuoteNode.h"
#include "boost/algorithm/string.hpp"
#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/exchange.h"
#include "StrategyNode.h"
#include "StrategySubSystem.h"
#include "BrokerSubSystem.h"
#include "Util/log.h"
#include "server.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/FunctionNode.h"
#include "Nodes/SignalNode.h"
#include "Nodes/NeuralNetworkNode.h"
#include "Nodes/DebugNode.h"
#include "Nodes/ScriptNode.h"
#include "Nodes/StackNode.h"
#include "Nodes/ExecuteNode.h"
#include "Nodes/PortfolioNode.h"
#include "Nodes/SpreadNode.h"
#include "Nodes/ProtectionNode.h"
#include "Nodes/EMDNode.h"
#include "Nodes/HMMNode.h"
#include "Nodes/ResampleNode.h"

namespace {
    Map<String, StrategyNodeType> node_type_map{
        {"input", StrategyNodeType::Input},
        {"lstm", StrategyNodeType::LSTM},
        {"function", StrategyNodeType::Function},
        {"feature", StrategyNodeType::Feature},
        {"signal", StrategyNodeType::Signal},
        {"debug", StrategyNodeType::Debug},
        {"execution", StrategyNodeType::Execution},
        {"portfolio", StrategyNodeType::Portfolio},
        {"test", StrategyNodeType::Test},
        {"spread", StrategyNodeType::Spread},
        {"protection", StrategyNodeType::Protection},
        {"emd", StrategyNodeType::EMD},
        {"hmm", StrategyNodeType::HMM},
        {"resample", StrategyNodeType::Resample},
    };

    Map<String, StatisticIndicator> statistics{
        {"sharp", StatisticIndicator::Sharp},
        {"annual_sharp", StatisticIndicator::AnualSharp},
        {"VAR", StatisticIndicator::VaR},
        {"ES", StatisticIndicator::ES},
        {"winRate", StatisticIndicator::WinRate},
        {"annualReturn", StatisticIndicator::AnualReturn},
        {"totalReturn", StatisticIndicator::TotalReturn},
        {"maxDrawdown", StatisticIndicator::MaxDrawDown},
        {"CalmarRatio", StatisticIndicator::Calmar}
    };
}

QNode* generate_input_node(const String& id, Server* server) {
    auto node = new QuoteInputNode(server);
    node->setID(atoi(id.c_str()));
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

template<typename T>
QNode* generate_node(const String& id) {
    auto node = new T();
    node->setID(atoi(id.c_str()));
    return node;
}

template<typename T>
QNode* generate_node(const String& id, Server* server) {
    auto node = new T(server);
    node->setID(atoi(id.c_str()));
    return node;
}

List<QNode*> parse_strategy_script_v2(const nlohmann::json& content, Server* server, SlippageConfigInfo* outSlippageConfig) {
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
            nodeInstance = generate_node<QuoteInputNode>(node["id"], server);
            break;
        case StrategyNodeType::Feature:
            break;
        case StrategyNodeType::LSTM:
            // nodeInstance = generate_node<LSTMNode>(node["id"]);
            break;
        case StrategyNodeType::Function:
            nodeInstance = generate_node<FunctionNode>(node["id"], server);
            break;
        case StrategyNodeType::Signal:
            nodeInstance = generate_signal_node(strategyName, node["id"], server);
            break;
        case StrategyNodeType::Debug:
            nodeInstance = generate_node<DebugNode>(node["id"], server);
            break;
        case StrategyNodeType::Script:
            nodeInstance = generate_node<ScriptNode>(node["id"]);
            break;
        case StrategyNodeType::Stack:
            nodeInstance = generate_node<StackNode>(node["id"]);
            break;
        case StrategyNodeType::Execution:
            nodeInstance = generate_node<ExecuteNode>(node["id"], server);
            break;
        case StrategyNodeType::Portfolio:
            nodeInstance = generate_node<PortfolioNode>(node["id"], server);
            break;
        case StrategyNodeType::Test:
            nodeInstance = generate_node<TestNode>(node["id"], server);
            break;
        case StrategyNodeType::Spread:
            nodeInstance = generate_node<SpreadNode>(node["id"]);
            break;
        case StrategyNodeType::Protection:
            nodeInstance = generate_node<ProtectionNode>(node["id"], server);
            break;
        case StrategyNodeType::EMD:
            nodeInstance = generate_node<EMDNode>(node["id"], server);
            break;
        case StrategyNodeType::HMM:
            nodeInstance = generate_node<HMMNode>(node["id"], server);
            break;
        case StrategyNodeType::Resample:
            nodeInstance = generate_node<ResampleNode>(node["id"], server);
            break;
        default:
            WARN("Unknown node type: {}", node_type);
            continue;
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

    // 收集滑点配置（从 ExecuteNode 中提取）
    if (outSlippageConfig) {
        Set<String> sources;
        nlohmann::json slippageConfig;
        bool hasSlippageConfig = false;

        for (auto& [id, node] : nodeMap) {
            if (dynamic_cast<ExecuteNode*>(node)) {
                auto& cfg = nodeConfigMap[id];
                if (cfg.contains("params") && cfg["params"].contains("slippageModel")) {
                    auto& slippageModel = cfg["params"]["slippageModel"];
                    int modelType = slippageModel["value"].get<int>();

                    // 构建滑点模型 JSON 配置
                    slippageConfig["type"] = modelType;
                    if (modelType == 0) {
                        double slippageValue = 0.0;
                        if (cfg["params"].contains("slippage") && cfg["params"]["slippage"].contains("value")) {
                            slippageValue = cfg["params"]["slippage"]["value"].get<double>();
                        }
                        slippageConfig["ratio"] = slippageValue;
                    } else {
                        slippageConfig["base"] = cfg["params"]["slippageBase"]["value"].get<double>();
                        slippageConfig["impact_k"] = cfg["params"]["slippageImpactK"]["value"].get<double>();
                        slippageConfig["alpha"] = cfg["params"]["slippageAlpha"]["value"].get<double>();
                    }
                    hasSlippageConfig = true;
                }
            }
        }

        // 收集所有 QuoteInputNode 的 source
        for (auto& [id, node] : nodeMap) {
            if (auto* quoteNode = dynamic_cast<QuoteInputNode*>(node)) {
                sources.insert(quoteNode->GetSource());
            }
        }

        if (hasSlippageConfig && !sources.empty()) {
            outSlippageConfig->sources = std::move(sources);
            outSlippageConfig->modelConfig = std::move(slippageConfig);
        }
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
