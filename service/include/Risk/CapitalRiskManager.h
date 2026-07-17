#pragma once
#include "Util/system.h"
#include "json.hpp"
#include <Eigen/Dense>
#include <mutex>
#include <functional>

class Server;
class BrokerSubSystem;

/**
 * 资金风控配置
 */
struct CapitalRiskConfig {
    // 总资金止损线（相对于初始本金的百分比）
    double _totalStopLossPercent = 0.85;

    // 单日最大亏损限额（相对于昨日权益的百分比）
    double _dailyMaxLossPercent = 0.03;

    bool _enableTotalStopLoss = false;
    bool _enableDailyLossLimit = false;
    bool _autoClosePosition = false;

    double _initialCapital = 0;
    double _lastDayEquity = 0;

    bool _isTotalStopLossTriggered = false;
    bool _isDailyLossTriggered = false;

    time_t _lastTradeDate = 0;
    int _manualInterventionTimeoutSec = 300;
    bool _timeoutAutoClose = true;
    time_t _alertTime = 0;
    bool _hasAlerted = false;

    // ── VaR 配置 ──
    bool _enableVaRLimit = false;
    double _varLimit = 0.02;            // VaR 上限（占总资金比例）
    double _varConfidence = 0.95;       // VaR 置信度
    int _varLookback = 60;              // VaR 回看窗口（天）

    // ── 回撤断路器配置 ──
    bool _enableDrawdownBreaker = false;
    double _ddLevel1 = 0.05;            // 警戒线（回撤 > 此值禁止新开仓）
    double _ddLevel2 = 0.10;            // 减仓线（回撤 > 此值持仓减半）
    double _ddLevel3 = 0.15;            // 熔断线（回撤 > 此值全部平仓+停机）
    bool _ddDynamicThresholds = false;  // 是否使用 Bootstrap 动态阈值
    int _ddBootstrapSamples = 1000;     // Bootstrap 重采样次数
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

    // ── VaR 计算 ──

    /**
     * 历史模拟法计算 VaR
     * @param returns 日收益率序列
     * @param confidence 置信度 (0.95)
     * @return VaR 值（正数表示损失）
     */
    static double computeHistoricalVaR(
        const Vector<double>& returns, double confidence);

    /**
     * 更新 VaR 状态（每个 Bar 结束时调用）
     * @param dailyReturn 当日收益率
     * @return VaR 是否超限
     */
    bool updateVaR(double dailyReturn);

    /**
     * 获取当前 VaR 值
     */
    double GetCurrentVaR() const { return _currentVaR; }

    /**
     * VaR 是否超限
     */
    bool IsVaRBreached() const { return _varBreached; }

    // ── 回撤断路器 ──

    /**
     * 更新回撤断路器状态
     * @param currentEquity 当前权益
     * @return 断路器级别 (0/1/2/3)
     */
    int updateDrawdownBreaker(double currentEquity);

    /**
     * 获取当前断路器级别
     */
    int GetBreakerLevel() const { return _breakerLevel; }

    /**
     * 获取当前回撤（从峰值计算）
     */
    double GetCurrentDrawdownFromPeak() const { return _currentDrawdownFromPeak; }

    /**
     * 人工解除熔断（Level 3 恢复）
     */
    void ResetBreaker();

    /**
     * 从历史收益率计算动态回撤阈值（Bootstrap）
     */
    void computeDynamicDrawdownThresholds(
        const Vector<double>& returns, int nBootstrap = 1000);

    // ── 风险贡献减仓 ──

    /**
     * 按风险贡献比例计算减仓后的新权重
     * @param weights 当前权重 {symbol → weight}
     * @param covMatrix 协方差矩阵 (Eigen::MatrixXd, n×n)
     * @param symbols 标的列表（与协方差矩阵行列对应）
     * @param targetScale 目标缩放比例 (0.7 = 风险降30%)
     * @return 减仓后的新权重
     */
    static Map<symbol_t, double> reduceByRiskContribution(
        const Map<symbol_t, double>& weights,
        const Eigen::MatrixXd& covMatrix,
        const Vector<symbol_t>& symbols,
        double targetScale);

    /**
     * 获取风控状态摘要（供 API 返回）
     */
    nlohmann::json GetRiskStatus() const;

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

    // ── VaR 状态 ──
    Vector<double> _dailyReturns;       // 滚动日收益率（VaR 计算用）
    double _currentVaR = 0;             // 当前 VaR 值
    bool _varBreached = false;          // VaR 是否超限

    // ── 回撤断路器状态 ──
    int _breakerLevel = 0;              // 当前断路器级别 (0/1/2/3)
    double _peakEquity = 0;             // 历史最高权益
    double _currentDrawdownFromPeak = 0; // 当前回撤（从峰值）
    bool _breakerTripped = false;       // Level 3 熔断激活（需人工恢复）
};
