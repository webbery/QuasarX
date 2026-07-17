#include "Risk/CapitalRiskManager.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include <format>
#include <fstream>
#include <atomic>
#include <algorithm>
#include <random>
#include <cmath>

CapitalRiskManager::CapitalRiskManager(Server* server)
    : _server(server), _cachedEquity(0), _lastCacheTime(0) {
}

CapitalRiskManager::~CapitalRiskManager() {
}

bool CapitalRiskManager::Init(const nlohmann::json& config) {
    std::lock_guard<std::mutex> lock(_mutex);

    FromJson(config);

    // 如果配置中有初始本金，设置它
    if (config.contains("initialCapital")) {
        _config._initialCapital = config["initialCapital"];
    }

    // 设置昨日权益为初始本金（如果没有设置）
    if (_config._lastDayEquity == 0 && _config._initialCapital > 0) {
        _config._lastDayEquity = _config._initialCapital;
    }

    return true;
}

void CapitalRiskManager::SetConfig(const CapitalRiskConfig& config) {
    std::lock_guard<std::mutex> lock(_mutex);
    _config = config;
}

void CapitalRiskManager::SetInitialCapital(double capital) {
    std::lock_guard<std::mutex> lock(_mutex);
    _config._initialCapital = capital;
    if (_config._lastDayEquity == 0) {
        _config._lastDayEquity = capital;
    }
}

void CapitalRiskManager::SetLastDayEquity(double equity) {
    std::lock_guard<std::mutex> lock(_mutex);
    _config._lastDayEquity = equity;
    // 重置单日触发状态
    _config._isDailyLossTriggered = false;
    _config._hasAlerted = false;
    _config._alertTime = 0;
}

double CapitalRiskManager::GetCurrentEquity() {
    // 从 BrokerSubSystem 获取当前持仓和资金
    auto broker = _server->GetBrokerSubSystem();
    if (!broker) {
        return _config._initialCapital;
    }

    // 获取当前总资产（持仓市值 + 可用资金）
    double totalEquity = 0;

    // 通过 Server 获取当前持仓信息
    auto& positions = _server->GetPosition();

    double positionValue = 0;
    for (auto& pos : positions._positions) {
        // 持仓市值 = 持仓数量 * 当前价格
        positionValue += pos._holds * pos._curPrice;
    }

    // TODO: 获取可用资金 - 需要从 Broker 或配置中获取
    // 暂时简化处理：假设可用资金 = 初始本金 - 持仓成本
    double cash = _config._initialCapital - positionValue;

    totalEquity = positionValue + cash;

    // 缓存结果
    _cachedEquity = totalEquity;
    _lastCacheTime = time(nullptr);

    return totalEquity;
}

double CapitalRiskManager::GetCurrentProfitLoss() {
    return GetCurrentEquity() - _config._initialCapital;
}

double CapitalRiskManager::GetDailyProfitLoss() {
    return GetCurrentEquity() - _config._lastDayEquity;
}

double CapitalRiskManager::GetTotalStopLossLevel() const {
    return _config._initialCapital * _config._totalStopLossPercent;
}

double CapitalRiskManager::GetDailyLossLimitLevel() const {
    return _config._lastDayEquity * (1.0 - _config._dailyMaxLossPercent);
}

double CapitalRiskManager::GetCurrentDrawdown() {
    if (_config._initialCapital <= 0) {
        return 0;
    }
    double currentEquity = _cachedEquity;
    return (currentEquity - _config._initialCapital) / _config._initialCapital;
}

double CapitalRiskManager::GetDailyLossPercent() {
    if (_config._lastDayEquity <= 0) {
        return 0;
    }
    double currentEquity = _cachedEquity;
    return (currentEquity - _config._lastDayEquity) / _config._lastDayEquity;
}

bool CapitalRiskManager::IsNewTradingDay(time_t currentTime) {
    if (_config._lastTradeDate == 0) {
        _config._lastTradeDate = currentTime;
        return false;
    }

    // 检查是否是不同的自然日
    struct tm last_tm, curr_tm;
#ifdef WIN32
    localtime_s(&last_tm, &_config._lastTradeDate);
    localtime_s(&curr_tm, &currentTime);
#else
    localtime_r(&_config._lastTradeDate, &last_tm);
    localtime_r(&currentTime, &curr_tm);
#endif

    if (last_tm.tm_mday != curr_tm.tm_mday ||
        last_tm.tm_mon != curr_tm.tm_mon ||
        last_tm.tm_year != curr_tm.tm_year) {
        return true;
    }

    return false;
}

bool CapitalRiskManager::IsManualInterventionTimeout() {
    if (_config._manualInterventionTimeoutSec <= 0) {
        return false;
    }

    if (_config._alertTime == 0) {
        return false;
    }

    time_t now = time(nullptr);
    double elapsed = difftime(now, _config._alertTime);

    return elapsed >= _config._manualInterventionTimeoutSec;
}

bool CapitalRiskManager::CheckRisk(double currentEquity) {
    std::lock_guard<std::mutex> lock(_mutex);

    // 缓存当前权益
    _cachedEquity = currentEquity;
    _lastCacheTime = time(nullptr);

    // 检查是否跨日，如果是则重置单日风控
    if (IsNewTradingDay(time(nullptr))) {
        ResetDaily();
    }

    bool riskTriggered = false;

    // 检查总资金止损
    if (_config._enableTotalStopLoss && !_config._isTotalStopLossTriggered) {
        double stopLossLevel = GetTotalStopLossLevel();
        if (currentEquity <= stopLossLevel) {
            _config._isTotalStopLossTriggered = true;
            _config._hasAlerted = false;
            riskTriggered = true;

            WARN("Total capital stop loss triggered! Current equity: {:.2f}, Stop loss level: {:.2f}",
                 currentEquity, stopLossLevel);
        }
    }

    // 检查单日亏损限额
    if (_config._enableDailyLossLimit && !_config._isDailyLossTriggered) {
        double dailyLossLevel = GetDailyLossLimitLevel();
        if (currentEquity <= dailyLossLevel) {
            _config._isDailyLossTriggered = true;
            _config._hasAlerted = false;
            riskTriggered = true;

            WARN("Daily loss limit triggered! Current equity: {:.2f}, Daily loss level: {:.2f}",
                 currentEquity, dailyLossLevel);
        }
    }

    // 如果触发了风控，发送报警或执行平仓
    if (riskTriggered) {
        if (_config._isTotalStopLossTriggered) {
            SendAlert(std::format("总资金止损线已触发！当前资产：{:.2f}, 止损线：{:.2f}",
                                    currentEquity, GetTotalStopLossLevel()), true);
        }
        if (_config._isDailyLossTriggered) {
            SendAlert(std::format("单日亏损限额已触发！当前资产：{:.2f}, 日亏损线：{:.2f}",
                                    currentEquity, GetDailyLossLimitLevel()), false);
        }

        // 如果配置了自动平仓，立即执行
        if (_config._autoClosePosition) {
            ExecuteClosePosition();
        }
    }

    // 检查人工介入超时（已触发风控但未平仓的情况下）
    if ((_config._isTotalStopLossTriggered || _config._isDailyLossTriggered) &&
        !_config._autoClosePosition &&
        !_config._hasAlerted) {
        // 首次触发时记录时间
        _config._alertTime = time(nullptr);
        _config._hasAlerted = true;
    } else if ((_config._isTotalStopLossTriggered || _config._isDailyLossTriggered) &&
               !_config._autoClosePosition &&
               IsManualInterventionTimeout()) {
        // 超时后自动平仓
        if (_config._timeoutAutoClose) {
            WARN("Manual intervention timeout, executing auto close position.");
            ExecuteClosePosition();
        }
    }

    return riskTriggered;
}

void CapitalRiskManager::Reset() {
    std::lock_guard<std::mutex> lock(_mutex);
    _config._isTotalStopLossTriggered = false;
    _config._isDailyLossTriggered = false;
    _config._hasAlerted = false;
    _config._alertTime = 0;
}

void CapitalRiskManager::ResetDaily() {
    std::lock_guard<std::mutex> lock(_mutex);
    _config._isDailyLossTriggered = false;
    _config._hasAlerted = false;
    _config._alertTime = 0;
    _config._lastTradeDate = time(nullptr);
}

void CapitalRiskManager::SendAlert(const String& message, bool isTotalStopLoss) {
    WARN("[CAPITAL RISK ALERT] {}", message);

    if (_alertCallback) {
        _alertCallback(message, isTotalStopLoss);
    }

    // TODO: 发送邮件/短信通知
    _server->SendEmail(message);
}

void CapitalRiskManager::ExecuteClosePosition() {
    WARN("[CAPITAL RISK] Executing close all positions...");

    if (_closePositionCallback) {
        _closePositionCallback();
    } else {
        // 默认平仓逻辑：调用 BrokerSubSystem 平仓
        CloseAllPositions();
    }
}

void CapitalRiskManager::CloseAllPositions() {
    // 获取所有持仓并平仓
    auto& positions = _server->GetPosition();
    auto broker = _server->GetBrokerSubSystem();

    if (!broker) {
        WARN("[CAPITAL RISK] BrokerSubSystem not available, cannot close positions.");
        return;
    }

    // 重置计数器
    _pendingCloseOrders = 0;
    _successCloseCount = 0;
    _failedCloseCount = 0;

    int orderCount = 0;

    for (auto& pos : positions._positions) {
        if (pos._validHolds > 0) {
            // 创建市价卖出订单
            Order sellOrder;
            sellOrder._symbol = pos._symbol;
            sellOrder._volume = pos._validHolds;  // 卖出可用数量
            sellOrder._type = OrderType::Market;
            sellOrder._side = 1;  // 1=卖出，0=买入
            sellOrder._price = 0;  // 市价单
            sellOrder._flag = 0;   // 非期货，无开平仓标志
            sellOrder._exec = false;
            sellOrder._validTime = OrderTimeValid::Today;
            sellOrder._hedge = OptionHedge::Speculation;
            sellOrder._status = OrderStatus::OrderAccept;
            sellOrder._time = time(nullptr);
            sellOrder._sysID = "";

            // 增加待处理订单计数
            _pendingCloseOrders++;

            // 创建回调函数，监听平仓结果
            auto callback = [this, symbol = pos._symbol](const TradeReport& report) {
                if (report._status == OrderStatus::OrderSuccess ||
                    report._status == OrderStatus::OrderPartSuccess) {
                    // 平仓成功
                    _successCloseCount++;
                    INFO("[CAPITAL RISK] Close position success for {}: {} shares @ {:.2f}",
                         get_symbol(symbol), report._quantity, report._price);
                } else if (report._status == OrderStatus::OrderFail ||
                           report._status == OrderStatus::OrderReject) {
                    // 平仓失败
                    _failedCloseCount++;
                    WARN("[CAPITAL RISK] Close position failed for {}: status={}",
                         get_symbol(symbol), (int)report._status);
                }
            };

            // 调用 Broker 卖出，使用 "system" 作为策略名，表示系统策略
            broker->Sell(0, "system", pos._symbol, sellOrder, callback);
            INFO("[CAPITAL RISK] Send close position order for {}: {} shares @ market",
                 get_symbol(pos._symbol), pos._validHolds);
            orderCount++;
        }
    }

    if (orderCount > 0) {
        SendAlert(std::format("Sent {} close orders. Waiting for execution...", orderCount), true);
    } else {
        SendAlert("No positions to close.", true);
    }
}

nlohmann::json CapitalRiskManager::ToJson() {
    nlohmann::json jsn;
    jsn["totalStopLossPercent"] = _config._totalStopLossPercent;
    jsn["dailyMaxLossPercent"] = _config._dailyMaxLossPercent;
    jsn["enableTotalStopLoss"] = _config._enableTotalStopLoss;
    jsn["enableDailyLossLimit"] = _config._enableDailyLossLimit;
    jsn["autoClosePosition"] = _config._autoClosePosition;
    jsn["initialCapital"] = _config._initialCapital;
    jsn["lastDayEquity"] = _config._lastDayEquity;
    jsn["isTotalStopLossTriggered"] = _config._isTotalStopLossTriggered;
    jsn["isDailyLossTriggered"] = _config._isDailyLossTriggered;
    jsn["manualInterventionTimeoutSec"] = _config._manualInterventionTimeoutSec;
    jsn["timeoutAutoClose"] = _config._timeoutAutoClose;
    jsn["currentEquity"] = _cachedEquity;
    jsn["currentDrawdown"] = GetCurrentDrawdown();
    jsn["dailyProfitLoss"] = GetDailyProfitLoss();
    jsn["dailyLossPercent"] = GetDailyLossPercent();
    // VaR
    jsn["enableVaRLimit"] = _config._enableVaRLimit;
    jsn["varLimit"] = _config._varLimit;
    jsn["varConfidence"] = _config._varConfidence;
    jsn["varLookback"] = _config._varLookback;
    jsn["currentVaR"] = _currentVaR;
    jsn["varBreached"] = _varBreached;
    // 回撤断路器
    jsn["enableDrawdownBreaker"] = _config._enableDrawdownBreaker;
    jsn["ddLevel1"] = _config._ddLevel1;
    jsn["ddLevel2"] = _config._ddLevel2;
    jsn["ddLevel3"] = _config._ddLevel3;
    jsn["breakerLevel"] = _breakerLevel;
    jsn["breakerTripped"] = _breakerTripped;
    jsn["drawdownFromPeak"] = _currentDrawdownFromPeak;
    jsn["peakEquity"] = _peakEquity;
    return jsn;
}

void CapitalRiskManager::FromJson(const nlohmann::json& jsn) {
    if (jsn.contains("totalStopLossPercent")) {
        _config._totalStopLossPercent = jsn["totalStopLossPercent"];
    }
    if (jsn.contains("dailyMaxLossPercent")) {
        _config._dailyMaxLossPercent = jsn["dailyMaxLossPercent"];
    }
    if (jsn.contains("enableTotalStopLoss")) {
        _config._enableTotalStopLoss = jsn["enableTotalStopLoss"];
    }
    if (jsn.contains("enableDailyLossLimit")) {
        _config._enableDailyLossLimit = jsn["enableDailyLossLimit"];
    }
    if (jsn.contains("autoClosePosition")) {
        _config._autoClosePosition = jsn["autoClosePosition"];
    }
    if (jsn.contains("manualInterventionTimeoutSec")) {
        _config._manualInterventionTimeoutSec = jsn["manualInterventionTimeoutSec"];
    }
    if (jsn.contains("timeoutAutoClose")) {
        _config._timeoutAutoClose = jsn["timeoutAutoClose"];
    }
    if (jsn.contains("initialCapital")) {
        _config._initialCapital = jsn["initialCapital"];
    }
    // VaR 配置
    if (jsn.contains("enableVaRLimit")) _config._enableVaRLimit = jsn["enableVaRLimit"];
    if (jsn.contains("varLimit")) _config._varLimit = jsn["varLimit"];
    if (jsn.contains("varConfidence")) _config._varConfidence = jsn["varConfidence"];
    if (jsn.contains("varLookback")) _config._varLookback = jsn["varLookback"];
    // 回撤断路器配置
    if (jsn.contains("enableDrawdownBreaker")) _config._enableDrawdownBreaker = jsn["enableDrawdownBreaker"];
    if (jsn.contains("ddLevel1")) _config._ddLevel1 = jsn["ddLevel1"];
    if (jsn.contains("ddLevel2")) _config._ddLevel2 = jsn["ddLevel2"];
    if (jsn.contains("ddLevel3")) _config._ddLevel3 = jsn["ddLevel3"];
    if (jsn.contains("ddDynamicThresholds")) _config._ddDynamicThresholds = jsn["ddDynamicThresholds"];
    if (jsn.contains("ddBootstrapSamples")) _config._ddBootstrapSamples = jsn["ddBootstrapSamples"];
}

// ── VaR 计算 ──────────────────────────────────────────────────

double CapitalRiskManager::computeHistoricalVaR(
    const Vector<double>& returns, double confidence)
{
    if (returns.empty()) return 0.0;

    Vector<double> sorted = returns;
    std::sort(sorted.begin(), sorted.end());

    // 取 (1-confidence) 分位数
    int idx = static_cast<int>((1.0 - confidence) * sorted.size());
    idx = std::clamp(idx, 0, static_cast<int>(sorted.size()) - 1);

    return -sorted[idx]; // 返回正值（损失）
}

bool CapitalRiskManager::updateVaR(double dailyReturn) {
    std::lock_guard<std::mutex> lock(_mutex);

    _dailyReturns.push_back(dailyReturn);

    // 保持滚动窗口
    int lookback = std::max(_config._varLookback, 20);
    if (static_cast<int>(_dailyReturns.size()) > lookback) {
        _dailyReturns.erase(_dailyReturns.begin());
    }

    // 需要足够数据才能计算
    if (static_cast<int>(_dailyReturns.size()) < 20) {
        _currentVaR = 0;
        _varBreached = false;
        return false;
    }

    _currentVaR = computeHistoricalVaR(_dailyReturns, _config._varConfidence);

    // VaR 占总资金比例
    double equity = _cachedEquity > 0 ? _cachedEquity : _config._initialCapital;
    double varRatio = (equity > 0) ? _currentVaR / equity : 0;

    _varBreached = _config._enableVaRLimit && (varRatio > _config._varLimit);

    if (_varBreached) {
        WARN("[VaR] Breach! VaR={:.4f} ({:.2f}% of equity), limit={:.2f}%",
             _currentVaR, varRatio * 100, _config._varLimit * 100);
    }

    return _varBreached;
}

// ── 回撤断路器 ────────────────────────────────────────────────

int CapitalRiskManager::updateDrawdownBreaker(double currentEquity) {
    std::lock_guard<std::mutex> lock(_mutex);

    // 更新峰值
    if (currentEquity > _peakEquity) {
        _peakEquity = currentEquity;
    }

    // 计算当前回撤
    if (_peakEquity > 0) {
        _currentDrawdownFromPeak = (_peakEquity - currentEquity) / _peakEquity;
    }

    double dd = _currentDrawdownFromPeak;

    // Level 3 熔断需要人工恢复
    if (_breakerTripped) {
        _breakerLevel = 3;
        return 3;
    }

    if (!_config._enableDrawdownBreaker) {
        _breakerLevel = 0;
        return 0;
    }

    // 三级判定
    int newLevel = 0;
    if (dd >= _config._ddLevel3) {
        newLevel = 3;
        _breakerTripped = true;  // 熔断锁定，需人工恢复
        WARN("[Breaker] Level 3 TRIPPED! Drawdown={:.2f}%, threshold={:.2f}%. Strategy HALTED.",
             dd * 100, _config._ddLevel3 * 100);
    } else if (dd >= _config._ddLevel2) {
        newLevel = 2;
        WARN("[Breaker] Level 2: Drawdown={:.2f}%, threshold={:.2f}%. Reduce positions by 50%.",
             dd * 100, _config._ddLevel2 * 100);
    } else if (dd >= _config._ddLevel1) {
        newLevel = 1;
        WARN("[Breaker] Level 1: Drawdown={:.2f}%, threshold={:.2f}%. Block new positions.",
             dd * 100, _config._ddLevel1 * 100);
    }

    _breakerLevel = newLevel;
    return newLevel;
}

void CapitalRiskManager::ResetBreaker() {
    std::lock_guard<std::mutex> lock(_mutex);
    _breakerTripped = false;
    _breakerLevel = 0;
    INFO("[Breaker] Manually reset. Breaker cleared.");
}

void CapitalRiskManager::computeDynamicDrawdownThresholds(
    const Vector<double>& returns, int nBootstrap)
{
    if (returns.size() < 20) {
        WARN("[Breaker] Not enough data for bootstrap (need 20+, got {})", returns.size());
        return;
    }

    int T = static_cast<int>(returns.size());
    Vector<double> maxdd_samples;
    maxdd_samples.reserve(nBootstrap);

    std::mt19937 rng(42); // 固定种子，结果可复现
    std::uniform_int_distribution<int> dist(0, T - 1);

    for (int b = 0; b < nBootstrap; ++b) {
        double peak = 1.0, cum = 1.0, maxdd = 0.0;
        for (int i = 0; i < T; ++i) {
            int idx = dist(rng);
            cum *= (1.0 + returns[idx]);
            if (cum > peak) peak = cum;
            double dd = (peak - cum) / peak;
            if (dd > maxdd) maxdd = dd;
        }
        maxdd_samples.push_back(maxdd);
    }

    std::sort(maxdd_samples.begin(), maxdd_samples.end());

    _config._ddLevel1 = maxdd_samples[nBootstrap * 0.50];
    _config._ddLevel2 = maxdd_samples[nBootstrap * 0.75];
    _config._ddLevel3 = maxdd_samples[nBootstrap * 0.95];

    INFO("[Breaker] Dynamic thresholds from {} bootstrap samples: L1={:.2f}%, L2={:.2f}%, L3={:.2f}%",
         nBootstrap, _config._ddLevel1 * 100, _config._ddLevel2 * 100, _config._ddLevel3 * 100);
}

// ── 风险贡献减仓 ──────────────────────────────────────────────

Map<symbol_t, double> CapitalRiskManager::reduceByRiskContribution(
    const Map<symbol_t, double>& weights,
    const Eigen::MatrixXd& covMatrix,
    const Vector<symbol_t>& symbols,
    double targetScale)
{
    int n = static_cast<int>(symbols.size());
    if (n == 0 || covMatrix.rows() != n || covMatrix.cols() != n) {
        return weights; // 无法计算，返回原权重
    }

    // 构造权重向量
    Eigen::VectorXd w(n);
    for (int i = 0; i < n; ++i) {
        auto it = weights.find(symbols[i]);
        w(i) = (it != weights.end()) ? it->second : 0.0;
    }

    // Σw
    Eigen::VectorXd Sw = covMatrix * w;

    // 组合波动率 σ_p = √(w'Σw)
    double sigma_p2 = w.dot(Sw);
    double sigma_p = std::sqrt(std::max(sigma_p2, 1e-12));

    // 风险贡献 RC_i = w_i × (Σw)_i / σ_p
    Eigen::VectorXd RC(n);
    double total_RC = 0;
    for (int i = 0; i < n; ++i) {
        RC(i) = w(i) * Sw(i) / sigma_p;
        total_RC += RC(i);
    }

    // 按风险贡献比例减仓
    double equal_share = total_RC / n;
    Map<symbol_t, double> new_weights;
    for (int i = 0; i < n; ++i) {
        double excess = (equal_share > 1e-12) ? RC(i) / equal_share : 1.0;
        double reduce = 1.0 - (1.0 - targetScale) * excess;
        reduce = std::max(reduce, 0.0);
        new_weights[symbols[i]] = w(i) * reduce;
    }

    return new_weights;
}

// ── 风控状态摘要 ──────────────────────────────────────────────

nlohmann::json CapitalRiskManager::GetRiskStatus() const {
    nlohmann::json status;
    status["breaker_level"] = _breakerLevel;
    status["current_drawdown"] = _currentDrawdownFromPeak;
    status["peak_equity"] = _peakEquity;
    status["breaker_tripped"] = _breakerTripped;

    status["var_current"] = _currentVaR;
    status["var_limit"] = _config._varLimit;
    status["var_breached"] = _varBreached;

    status["thresholds"] = {
        {"level1", _config._ddLevel1},
        {"level2", _config._ddLevel2},
        {"level3", _config._ddLevel3}
    };

    double equity = _cachedEquity > 0 ? _cachedEquity : _config._initialCapital;
    status["var_ratio"] = (equity > 0) ? _currentVaR / equity : 0;

    return status;
}
