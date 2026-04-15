#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Util/system.h"
#include "StrategyNode.h"

#define SCRIPTS_DIR     "scripts"

class StrategyPlugin;
class FeatureSubsystem;
class FlowSubsystem;
class Server;
class QNode;
enum class DataFrequencyType;
enum class StrategyType: char;
enum class StatisticIndicator: char;

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
    void InitStrategy(const String& strategy, const nlohmann::json& script);
    void InitStrategy(const String& strategy, const List<QNode*>& flow);

    void Release();
    void ReleaseStrategy(const String& strategy);

    /**
     * 运行载入的策略
     */
    bool Run(const String& strategy);
    /**
     * 停止运行策略
     */
    void Stop(const String& strategy);

    List<String> GetStrategyNames();

    /**
     * 从脚本中载入策略
     */
    bool InstallStrategy(const String& strategy);
    /**
     * 卸载内存中的策略
     */
    bool UninstallStrategy(const String& strategy);

    bool HasStrategy(const String& name);

    bool CreateStrategy(const String& name, const nlohmann::json& params);
    // bool AddStrategy(const AgentStrategyInfo& info);
    void DeleteStrategy(const String& name);
    
    void Train(const String& name, const Vector<symbol_t>& history, DataFrequencyType freq);

    Set<symbol_t> GetPools(const String& strategy);
    
    void SetupSimulation(const String& name);

    FeatureSubsystem* GetFeatureSystem() { return _featureSystem; }
    FlowSubsystem* GetFlowSubsystem() { return _agentSystem; }

    Map<StatisticIndicator, std::variant<float, List<float>>> GetIndicators(const String& strategy);

    /**
     * @brief 获取策略的预热期 epoch 数（回测模式）
     */
    int GetWarmupEpochs(const String& strategy) const;

private:
    // AgentStrategyInfo ParseJsonScript(const String& content);

private:
    FeatureSubsystem* _featureSystem;
    FlowSubsystem* _agentSystem;

    Set<String> _strategies;
    Set<String> _virtualStrategies;

    // 策略预热期配置（strategy -> warmup epochs）
    Map<String, int> _strategyWarmupEpochs;

    Server* _handle;
};