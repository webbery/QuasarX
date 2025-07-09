#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Util/system.h"
#include "Agents/IAgent.h"

class StrategyPlugin;
class FeatureSubsystem;
class AgentSubsystem;
class Server;
enum class DataFrequencyType;

struct FeatureInfo {
    String _type;
    nlohmann::json _params;
};

struct AgentInfo {
    AgentType _type;
    String _modelpath;
    nlohmann::json _params;
};

struct AgentStrategyInfo {
    String _name;
    char _level;    // N则表示预测第N天/0表示实时预测,交易执行器在第N天/实时执行
    List<String> _pool;
    List<FeatureInfo> _features;
    List<AgentInfo> _agents;
};

/**
 * @brief 策略系统,从配置脚本中加载并构建策略,启动特征服务线程和预测线程.
 *        配置脚本中的定义的特征会注册到特征服务线程中, 并通过消息机制将
 *        原始数据转为特征发送给预测线程,预测线程对未来做涨跌预测,并将结果
 *        发送出去
 */
class StrategySubSystem {
public:
    StrategySubSystem(Server* server);
    ~StrategySubSystem();

    void Init();

    void Release();

    //StrategyPlugin* GetOrCreateStrategy(const char* name);

    List<String> GetStrategyNames();

    bool HasStrategy(const String& name);

    bool CreateStrategy(const String& name, const nlohmann::json& params);
    bool AddStrategy(const AgentStrategyInfo& info);
    
    void Train(const String& name, const Vector<symbol_t>& history, DataFrequencyType freq);
private:
    AgentStrategyInfo ParseJsonScript(const String& content);

private:
    FeatureSubsystem* _featureSystem;
    AgentSubsystem* _agentSystem;

    Set<String> _strategies;

    Server* _handle;
};