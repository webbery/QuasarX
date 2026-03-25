#pragma once
#include "Util/system.h"
#include "json.hpp"
#include <mutex>
#include <functional>

class Server;
class BrokerSubSystem;

/**
 * 资金风控配置
 */
struct CapitalRiskConfig {
    // 总资金止损线（相对于初始本金的百分比）
    // 例如：0.85 表示当总资产跌至初始本金的 85% 时触发
    double _totalStopLossPercent = 0.85;

    // 单日最大亏损限额（相对于昨日权益的百分比）
    // 例如：0.03 表示当日亏损超过昨日权益的 3% 时触发
    double _dailyMaxLossPercent = 0.03;

    // 是否启用总资金止损
    bool _enableTotalStopLoss = false;

    // 是否启用单日亏损限额
    bool _enableDailyLossLimit = false;

    // 触及止损后是否自动平仓
    bool _autoClosePosition = false;

    // 初始本金
    double _initialCapital = 0;

    // 昨日权益（用于计算当日盈亏）
    double _lastDayEquity = 0;

    // 风控触发后的状态
    bool _isTotalStopLossTriggered = false;
    bool _isDailyLossTriggered = false;

    // 最后交易日（用于判断是否跨日）
    time_t _lastTradeDate = 0;

    // 人工介入超时时间（秒），0 表示不启用超时
    int _manualInterventionTimeoutSec = 300;

    // 超时后是否自动平仓
    bool _timeoutAutoClose = true;

    // 报警后等待人工介入的开始时间
    time_t _alertTime = 0;

    // 是否已发送报警
    bool _hasAlerted = false;
};

/**
 * 资金风险管理器
 * 监控总资金止损线和单日亏损限额
 */
class CapitalRiskManager {
public:
    using AlertCallback = std::function<void(const String& message, bool isTotalStopLoss)>;
    using ClosePositionCallback = std::function<void()>;

    CapitalRiskManager(Server* server);
    ~CapitalRiskManager();

    /**
     * 初始化
     */
    bool Init(const nlohmann::json& config);

    /**
     * 设置风控配置
     */
    void SetConfig(const CapitalRiskConfig& config);

    /**
     * 获取当前配置
     */
    const CapitalRiskConfig& GetConfig() const { return _config; }

    /**
     * 设置初始本金
     */
    void SetInitialCapital(double capital);

    /**
     * 设置昨日权益（每个交易日开始时调用）
     */
    void SetLastDayEquity(double equity);

    /**
     * 更新当前权益并检查风控
     * @param currentEquity 当前总资产
     * @return 是否触发风控（true=已触发，需要采取措施）
     */
    bool CheckRisk(double currentEquity);

    /**
     * 检查是否触发总资金止损
     */
    bool IsTotalStopLossTriggered() const { return _config._isTotalStopLossTriggered; }

    /**
     * 检查是否触发单日亏损限额
     */
    bool IsDailyLossTriggered() const { return _config._isDailyLossTriggered; }

    /**
     * 重置风控状态（用于人工干预后）
     */
    void Reset();

    /**
     * 重置单日触发状态（用于新交易日）
     */
    void ResetDaily();

    /**
     * 获取当前总资产（从 Broker 获取持仓和资金计算）
     */
    double GetCurrentEquity();

    /**
     * 获取当前总盈亏
     */
    double GetCurrentProfitLoss();

    /**
     * 获取当日盈亏
     */
    double GetDailyProfitLoss();

    /**
     * 获取总资金止损线对应的资产值
     */
    double GetTotalStopLossLevel() const;

    /**
     * 获取单日亏损限额对应的资产值
     */
    double GetDailyLossLimitLevel() const;

    /**
     * 获取当前回撤百分比
     */
    double GetCurrentDrawdown();

    /**
     * 获取当日亏损百分比
     */
    double GetDailyLossPercent();

    /**
     * 设置报警回调
     */
    void SetAlertCallback(AlertCallback cb) { _alertCallback = cb; }

    /**
     * 设置自动平仓回调
     */
    void SetClosePositionCallback(ClosePositionCallback cb) { _closePositionCallback = cb; }

    /**
     * 手动触发全部平仓
     */
    void CloseAllPositions();

    /**
     * 序列化配置为 JSON
     */
    nlohmann::json ToJson();

    /**
     * 从 JSON 加载配置
     */
    void FromJson(const nlohmann::json& jsn);

private:
    /**
     * 发送报警
     */
    void SendAlert(const String& message, bool isTotalStopLoss);

    /**
     * 执行自动平仓
     */
    void ExecuteClosePosition();

    /**
     * 检查是否已跨日（用于自动重置单日风控）
     */
    bool IsNewTradingDay(time_t currentTime);

    /**
     * 检查人工介入是否超时
     */
    bool IsManualInterventionTimeout();

private:
    Server* _server;
    CapitalRiskConfig _config;

    std::mutex _mutex;

    AlertCallback _alertCallback;
    ClosePositionCallback _closePositionCallback;

    // 缓存的当前权益
    double _cachedEquity;
    time_t _lastCacheTime;

    // 平仓统计
    std::atomic<int> _pendingCloseOrders{0};
    std::atomic<int> _successCloseCount{0};
    std::atomic<int> _failedCloseCount{0};
};
