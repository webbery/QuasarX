#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Util/system.h"
#include "Util/DailyDecision.h"
#include "StrategyNode.h"
#include <mutex>

#define SCRIPTS_DIR     "scripts"

// 策略最低兼容版本，低于此版本的策略将不会被加载
constexpr int MIN_STRATEGY_VERSION = 1;

class StrategyPlugin;
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
    // TODO: 获取日级策略名
    List<String> GetDailyStrategyNames();

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

    // 获取某个策略的依赖标的
    Set<symbol_t> GetPools(const String& strategy);
    
    void SetupSimulation(const String& name);

    FlowSubsystem* GetFlowSubsystem() { return _agentSystem; }

    Map<StatisticIndicator, std::variant<float, List<float>>> GetIndicators(const String& strategy);

    /**
     * @brief 获取策略的预热期 epoch 数（回测模式）
     */
    int GetWarmupEpochs(const String& strategy) const;

    // ── 日级策略执行（收盘后依赖驱动）──

    /**
     * @brief 初始化日级执行状态
     * 
     * 从已加载策略中提取依赖关系，建立策略→标的映射
     * 应在每日 15:00 前调用
     */
    void InitDailyExecution();

    /**
     * @brief 重置每日状态
     * 
     * 清空已就绪标的和已执行策略标记
     * 应在每日 15:00 前调用（InitDailyExecution 之后）
     */
    void ResetDaily();

    /**
     * @brief 标记标的后复权数据就绪
     * 
     * 内部检查：是否有策略的所有依赖已就绪
     * 如果有，异步执行该策略（不阻塞调用线程）
     * 
     * @param symbol 标的代码（如 "sz.000001"）
     */
    void MarkSymbolReady(const String& symbol);

    /**
     * @brief 强制执行所有未执行的策略（超时兜底）
     * 
     * 应在 15:30 调用，确保所有策略都能执行
     */
    void ForceExecuteAllDaily();

    /**
     * @brief 获取日级执行状态（供前端查询）
     */
    nlohmann::json GetDailyStatus() const;

private:
    // 执行单个日级策略并保存决策
    void ExecuteDailyStrategy(const String& strategy);

private:
    // AgentStrategyInfo ParseJsonScript(const String& content);

private:
    FlowSubsystem* _agentSystem;

    Set<String> _strategies;
    Set<String> _virtualStrategies;

    // 策略预热期配置（strategy -> warmup epochs）
    Map<String, int> _strategyWarmupEpochs;

    // ── 日级执行状态 ──
    Map<String, Set<String>> _dailyStrategySymbols;  // 策略→依赖标的
    Set<String> _dailyReadySymbols;                   // 已就绪标的
    Set<String> _dailyExecutedStrategies;             // 已执行策略
    bool _dailyInitialized = false;
    mutable std::mutex _dailyMtx;

    Server* _handle;
};