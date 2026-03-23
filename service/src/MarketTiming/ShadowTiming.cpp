#include "MarketTiming/ShadowTiming.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "server.h"
#include <filesystem>
#include <chrono>
#include <iomanip>

// ============== ShadowAccount 实现 ==============

ShadowAccount::ShadowAccount(double initialCapital)
    : virtualCapital(initialCapital), virtualAvailable(initialCapital) {
}

bool ShadowAccount::TryFreezeFunds(double cost) {
    double expected = virtualAvailable.load(std::memory_order_relaxed);
    while (true) {
        if (expected < cost) {
            WARN("[Shadow] 虚拟资金不足：所需 {:.2f}，可用 {:.2f}", cost, expected);
            return false;
        }
        if (virtualAvailable.compare_exchange_strong(expected, expected - cost,
                std::memory_order_release, std::memory_order_relaxed)) {
            return true;
        }
        // expected 已被更新为当前值，继续循环重试
    }
}

void ShadowAccount::ReleaseFunds(double amount) {
    virtualAvailable.fetch_add(amount, std::memory_order_release);
}

void ShadowAccount::UpdatePosition(symbol_t symbol, int64_t delta) {
    std::lock_guard<std::mutex> lock(accountMutex);
    shadowPositions[symbol] += delta;
}

int64_t ShadowAccount::GetPosition(symbol_t symbol) const {
    std::lock_guard<std::mutex> lock(accountMutex);
    auto it = shadowPositions.find(symbol);
    if (it != shadowPositions.end()) {
        return it->second;
    }
    return 0;
}

// ============== ShadowTiming 实现 ==============

ShadowTiming::ShadowTiming(Server* server, const ShadowConfig& config)
    : ITimingStrategy(server), _config(config) {
    _account = std::make_unique<ShadowAccount>(config.initialCapital);
    InitLogFile();

    INFO("[Shadow] 影子模式启动 - 初始资金：{:.2f}, 滑点：{:.4f}",
         config.initialCapital, config.slippageRate);
}

ShadowTiming::~ShadowTiming() {
    if (_logFile.is_open()) {
        _logFile.close();
    }
}

void ShadowTiming::InitLogFile() {
    // 创建日志目录
    std::filesystem::create_directories(_config.logPath);

    // 生成日志文件名：shadow_YYYYMMDD.log
    String filename = _config.logPath + "/shadow_" + GetDateStr() + ".log";

    _logFile.open(filename, std::ios::app);
    if (!_logFile.is_open()) {
        WARN("[Shadow] 无法打开日志文件：{}", filename);
        return;
    }

    // 写入文件头（如果是新文件）
    if (_logFile.tellp() == 0) {
        _logFile << "# 时间戳 | 策略 | 标的 | 动作 | 预期价 | 预期量 | 模拟成交价 | 模拟成交量 | 状态 | 虚拟资金\n";
        _logFile.flush();
    }
}

String ShadowTiming::GetDateStr() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_now = localtime(&time_t_now);

    std::ostringstream oss;
    oss << std::put_time(tm_now, "%Y%m%d");
    return oss.str();
}

bool ShadowTiming::GetCurrentBar(symbol_t symbol, QuoteInfo& bar) {
    // 从 Server 获取最新行情（备用方案）
    auto exchange = _server->GetAvaliableStockExchange();
    if (exchange) {
        bar = exchange->GetQuote(symbol);
        return !is_null(bar._symbol) && bar._time > 0;
    }
    return false;
}

bool ShadowTiming::processSignal(const String& strategy, const TradeSignal& signal,
                                  const DataContext& context) {
    auto symbol = signal.GetSymbol();
    auto action = signal.GetAction();
    auto expectedPrice = signal.GetPrice();
    auto expectedQty = signal.GetQuantity();

    // 跳过 HOLD 操作
    if (action == TradeAction::HOLD) {
        return true;
    }

    // 从 DataContext 获取当前 Bar 数据（优先）
    const QuoteInfo* bar = context.GetQuote(symbol);

    // 如果 context 中没有，则从 Server 获取（备用方案）
    QuoteInfo fallbackBar;
    if (!bar) {
        if (GetCurrentBar(symbol, fallbackBar)) {
            bar = &fallbackBar;
        } else {
            WARN("[Shadow] 无法获取 {} 的行情数据，跳过信号", get_symbol(symbol));
            return false;
        }
    }

    // 估算成交
    ShadowFillResult fillResult;
    if (_config.estimateFill) {
        // 根据订单类型选择估算方法
        // 简单判断：如果信号价格 >0 且接近 Bar 价格，认为是限价单
        bool isLimitOrder = (expectedPrice > 0);

        if (isLimitOrder) {
            fillResult = EstimateLimitFill(signal, *bar);
        } else {
            fillResult = EstimateMarketFill(signal, *bar);
        }
    } else {
        // 不估算成交，假设全部成交
        fillResult.filled = true;
        fillResult.fillPrice = expectedPrice;
        fillResult.fillQty = expectedQty;
        fillResult.status = "FILLED";
    }

    // 如果成交，更新虚拟账户
    if (fillResult.filled) {
        double tradeAmount = fillResult.fillPrice * fillResult.fillQty;

        if (action == TradeAction::BUY) {
            // 买入：冻结资金
            if (_account->TryFreezeFunds(tradeAmount)) {
                _account->UpdatePosition(symbol, fillResult.fillQty);
                DEBUG_INFO("[Shadow] {} 买入成交：{} 股 @ {:.2f}, 金额：{:.2f}",
                          get_symbol(symbol), fillResult.fillQty, fillResult.fillPrice, tradeAmount);
            } else {
                // 虚拟资金不足，拒绝成交
                fillResult.status = "REJECTED";
                fillResult.filled = false;
                WARN("[Shadow] {} 虚拟资金不足，拒绝买入", get_symbol(symbol));
            }
        } else if (action == TradeAction::SELL) {
            // 卖出：检查持仓并释放资金
            int64_t currentPos = _account->GetPosition(symbol);
            if (currentPos >= fillResult.fillQty) {
                _account->UpdatePosition(symbol, -fillResult.fillQty);
                _account->ReleaseFunds(tradeAmount);
                DEBUG_INFO("[Shadow] {} 卖出成交：{} 股 @ {:.2f}, 金额：{:.2f}",
                          get_symbol(symbol), fillResult.fillQty, fillResult.fillPrice, tradeAmount);
            } else {
                // 持仓不足，拒绝成交
                fillResult.status = "REJECTED";
                fillResult.filled = false;
                WARN("[Shadow] {} 持仓不足 (当前：{}/需要：{}), 拒绝卖出",
                    get_symbol(symbol), currentPos, fillResult.fillQty);
            }
        }

        // 记录到 TradeReport 列表（可选，用于后续分析）
        if (fillResult.filled) {
            TradeReport report;
            report._price = fillResult.fillPrice;
            report._quantity = fillResult.fillQty;
            report._time = bar->_time;
            report._side = (action == TradeAction::BUY) ? 0 : 1;
            report._trade_amount = tradeAmount;
            report._status = OrderStatus::OrderSuccess;
            _reports.emplace_back(std::make_pair(symbol, report));
        }
    }

    // 记录影子日志
    LogShadowSignal(strategy, signal, fillResult);

    return true;
}

ShadowFillResult ShadowTiming::EstimateLimitFill(const TradeSignal& signal, const QuoteInfo& bar) {
    ShadowFillResult result;

    auto action = signal.GetAction();
    double limitPrice = signal.GetPrice();
    int64_t orderQty = signal.GetQuantity();

    // 应用滑点
    double slippage = limitPrice * _config.slippageRate;

    if (action == TradeAction::BUY) {
        // 买入限价单：当 Bar 最低价 ≤ 限价时成交
        // 成交价取 限价 和 最低价+滑点 的较好者
        double bestPrice = std::min(limitPrice, (double)bar._low + slippage);

        if (bar._low <= limitPrice) {
            result.filled = true;
            result.fillPrice = bestPrice;
            result.fillQty = orderQty;
            result.status = "FILLED";
        } else {
            // 最低价高于限价，未触及
            result.status = "PENDING";
            result.fillPrice = limitPrice;
            result.fillQty = 0;
        }
    }
    else if (action == TradeAction::SELL) {
        // 卖出限价单：当 Bar 最高价 ≥ 限价时成交
        // 成交价取 限价 和 最高价 - 滑点 的较好者
        double bestPrice = std::max(limitPrice, (double)bar._high - slippage);

        if (bar._high >= limitPrice) {
            result.filled = true;
            result.fillPrice = bestPrice;
            result.fillQty = orderQty;
            result.status = "FILLED";
        } else {
            // 最高价低于限价，未触及
            result.status = "PENDING";
            result.fillPrice = limitPrice;
            result.fillQty = 0;
        }
    }

    return result;
}

ShadowFillResult ShadowTiming::EstimateMarketFill(const TradeSignal& signal, const QuoteInfo& bar) {
    ShadowFillResult result;

    auto action = signal.GetAction();
    int64_t orderQty = signal.GetQuantity();

    // 应用滑点
    double slippage = bar._close * _config.slippageRate;

    if (action == TradeAction::BUY) {
        // 市价买入：使用 Bar 开盘价 + 滑点
        result.filled = true;
        result.fillPrice = bar._open + slippage;
        result.fillQty = orderQty;
        result.status = "FILLED";
    }
    else if (action == TradeAction::SELL) {
        // 市价卖出：使用 Bar 开盘价 - 滑点
        result.filled = true;
        result.fillPrice = bar._open - slippage;
        result.fillQty = orderQty;
        result.status = "FILLED";
    }

    return result;
}

void ShadowTiming::LogShadowSignal(const String& strategy, const TradeSignal& signal,
                                    const ShadowFillResult& fillResult) {
    std::lock_guard<std::mutex> lock(_logMutex);

    if (!_logFile.is_open()) {
        return;
    }

    auto symbol = signal.GetSymbol();
    auto action = signal.GetAction();
    auto expectedPrice = signal.GetPrice();
    auto expectedQty = signal.GetQuantity();

    // 获取当前时间戳
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);

    // 格式化输出
    _logFile << time_t_now << " | "
             << strategy << " | "
             << get_symbol(symbol) << " | "
             << (action == TradeAction::BUY ? "BUY" : "SELL") << " | "
             << std::fixed << std::setprecision(2) << expectedPrice << " | "
             << expectedQty << " | "
             << std::fixed << std::setprecision(2) << fillResult.fillPrice << " | "
             << fillResult.fillQty << " | "
             << fillResult.status << " | "
             << std::fixed << std::setprecision(2) << _account->GetAvailableFunds()
             << "\n";

    _logFile.flush();
}
