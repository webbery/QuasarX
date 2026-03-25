#include "Risk/CapitalRiskManager.h"
#include "server.h"
#include "BrokerSubSystem.h"
#include "Bridge/exchange.h"
#include "Util/datetime.h"
#include <format>
#include <fstream>
#include <atomic>

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

    // 获取持仓
    auto& portfolio = broker->GetAllHistoryTrades();

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
    localtime_r(&_config._lastTradeDate, &last_tm);
    localtime_r(&currentTime, &curr_tm);

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
    // _server->SendEmail(message);
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
            broker->Sell("system", pos._symbol, sellOrder, callback);
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
}
