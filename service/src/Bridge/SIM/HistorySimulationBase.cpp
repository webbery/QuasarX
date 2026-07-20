#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Bridge/exchange.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "std_header.h"
#include "csv.h"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory_resource>
#include <numeric>
#include <thread>
#include <utility>
#include "yas/detail/type_traits/flags.hpp"
#include "server.h"
#include "BrokerSubSystem.h"

HistorySimulationBase::HistorySimulationBase(Server* server)
    : ExchangeInterface(server), _cur_id(0), _finish(false)
{
}

HistorySimulationBase::~HistorySimulationBase() {
    Clear();
}

bool HistorySimulationBase::Init(const ExchangeInfo& handle) {
    _org_path = handle._quote_addr;
    return true;
}

bool HistorySimulationBase::Release() {
    Clear();
    return true;
}

void HistorySimulationBase::SetFilter(const QuoteFilter& filter) {
    _filter = filter;

    if (!std::filesystem::exists(_org_path)) {
        WARN("{} not exist.", _org_path);
        return;
    }
}

bool HistorySimulationBase::Login(AccountType t) {
    _finish = false;
    Clear();

    for (auto& code : _filter._symbols) {
        if (!LoadData(code)) {
            WARN("Failed to load data for '{}'", code);
        }
    }

    OnDataLoaded();
    _dataLoadSuccess = true;
    return true;
}

bool HistorySimulationBase::IsLogin() {
    return !_finish;
}

void HistorySimulationBase::Logout(AccountType t) {
    _finish = true;
}

bool HistorySimulationBase::GetSymbolExchanges(List<Pair<String, ExchangeName>>& info) {
    return true;
}

order_id HistorySimulationBase::AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order) {
    BacktestContext* ctx = nullptr;
    _backtestContexts.visit(run_id, [&ctx](auto& item) {
        ctx = item.second.get();
    });

    if (ctx) {
        OrderInfo info;
        info._id = ++_cur_id;
        info._order = order;
        auto* queue = ctx->getOrCreateOrderQueue(symbol);
        queue->push(info);

        order_id id;
        id._id = info._id;
        if (is_stock(symbol)) {
            id._type = 0;
        }
        return id;
    }

    // 回退：找不到 context，只返回 id
    OrderInfo info;
    info._id = ++_cur_id;
    info._order = order;

    order_id id;
    id._id = info._id;
    if (is_stock(symbol)) {
        id._type = 0;
    }
    return id;
}

bool HistorySimulationBase::OrderReport(BacktestContext* context, order_id id, const TradeReport& report) {
    auto* orderCtx = context->getOrderReport(id._id);
    if (!orderCtx)
        return false;
    orderCtx->Update(report);
    orderCtx->_trades._reports.emplace_back(report);

    // 更新持仓（使用上下文私有持仓）
    const auto& order = orderCtx->_order;
    auto delta = (order._side == 0) ? report._quantity : -report._quantity;
    context->adjustPosition(orderCtx->_order._symbol, delta, report._price);

    orderCtx->_success.store(true);
    orderCtx->_flag.store(true);
    orderCtx->_promise.set_value(true);

    return true;
}

void HistorySimulationBase::OnOrderReport(order_id id, const TradeReport& report) {
    bool found = false;
    _backtestContexts.visit_all([&found, &id, &report, this](auto& item) {
        if (found) return;
        auto* ctx = item.second.get();
        found = OrderReport(ctx, id, report);
    });

    if (found) return;

    _reports.visit(id._id, [&report](auto&& value) {
        auto ctx = value.second;
        value.second->_trades._reports.emplace_back(report);
        ctx->Update(report);
        value.second->_success.store(true);
        value.second->_flag.store(true);
        value.second->_promise.set_value(true);
    });
}

Boolean HistorySimulationBase::CancelOrder(order_id id, OrderContext* order) {
    return true;
}

bool HistorySimulationBase::GetOrders(SecurityType type, OrderList& ol) {
    return true;
}

bool HistorySimulationBase::GetOrder(const String& sysID, Order& ol) {
    return true;
}

void HistorySimulationBase::QueryQuotes() {
    // 多线程回测模式下不需要主动查询，由 stepForward 推进时间
}

double HistorySimulationBase::GetAvailableFunds(run_id_t run_id) {
    // 默认返回 0.0，表示该 Exchange 没有该策略的资金上下文
    // DataContext::getAvailableCapital() 会对所有 Exchange 求和，
    // 如果这里返回默认值 500000，会导致多 Exchange 场景下资金被重复累加
    double funds = 0.0;
    _backtestContexts.visit(run_id, [&funds](auto& item) {
        funds = item.second->getAvailableFunds();
    });
    return funds;
}

bool HistorySimulationBase::GetCommission(symbol_t symbol, List<Commission>& comms) {
    return true;
}

Boolean HistorySimulationBase::HasPermission(symbol_t symbol) {
    return true;
}

void HistorySimulationBase::Reset() {
}

bool HistorySimulationBase::GetPosition(AccountPosition& pos) {
    return true;
}

AccountAsset HistorySimulationBase::GetAsset() {
    AccountAsset ass;
    return ass;
}

void HistorySimulationBase::SetCommission(const Commission& buy, const Commission& sell) {
    _buy = buy;
    _sell = sell;
}

int HistorySimulationBase::GetStockLimitation(char type) {
    return 0;
}

bool HistorySimulationBase::SetStockLimitation(char type, int limitation) {
    return false;
}

// ============ CSV 加载 ============

#define CACHE_SIZE 2048

bool HistorySimulationBase::LoadCSVToDataFrame(const String& file_path,
                                                DataFrame& df,
                                                Vector<String>& header) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return false;
    }

    INFO("load {} success", file_path);
    char cache[CACHE_SIZE] = {0};
    Vector<time_t> dates;
    Vector<float> open, close, high, low;
    Vector<int64_t> volume;
    header.clear();

    int index = 0;
    while (ifs.getline(cache, CACHE_SIZE)) {
        Vector<String> row;
        split(cache, row, ",");

        if (index++ == 0) {
            for (int i = 0; i < 6; ++i) {
                header.emplace_back(row[i]);
            }
            continue;
        }

        // 跳过空行或格式错误的行
        if (row.empty() || row[0].empty()) {
            --index;  // 不计数空行
            continue;
        }

        const char* timeFmt = (row[0].size() <= 10) ? "%Y-%m-%d" : "%Y-%m-%d %H:%M:%S";
        time_t timestamp = FromStr(row[0], timeFmt);

        // 跳过无效时间戳（FromStr 返回 -1 表示解析失败）
        if (timestamp < 0) {
            WARN("Invalid timestamp in CSV: {}, skipping row", row[0]);
            --index;
            continue;
        }

        dates.emplace_back(timestamp);
        open.emplace_back(std::stof(row[1]));
        close.emplace_back(std::stof(row[2]));
        high.emplace_back(std::stof(row[3]));
        low.emplace_back(std::stof(row[4]));
        volume.emplace_back(std::stol(row[5]));
    }
    ifs.close();

    if (header.empty() || dates.empty()) {
        return false;
    }

    size_t numRows = dates.size();
    Vector<uint32_t> indexes(numRows);
    std::iota(indexes.begin(), indexes.end(), 1);
    df.load_index(std::move(indexes));
    df.load_column(header[0].c_str(), std::move(dates));
    df.load_column(header[1].c_str(), std::move(open));
    df.load_column(header[2].c_str(), std::move(close));
    df.load_column(header[3].c_str(), std::move(high));
    df.load_column(header[4].c_str(), std::move(low));
    df.load_column(header[5].c_str(), std::move(volume));

    return true;
}

void HistorySimulationBase::BuildDataFrameFromMap(
    const Map<String, Vector<double>>& data,
    const Vector<String>& dates,
    DataFrame& df,
    Vector<String>& header)
{
    header = {"date", "open", "close", "high", "low", "volume"};

    Vector<time_t> timestamps;
    Vector<float> open, close, high, low;
    Vector<int64_t> volume;

    timestamps.reserve(dates.size());
    open.reserve(dates.size());
    close.reserve(dates.size());
    high.reserve(dates.size());
    low.reserve(dates.size());
    volume.reserve(dates.size());

    for (size_t i = 0; i < dates.size(); ++i) {
        const char* fmt = (dates[i].size() <= 10) ? "%Y-%m-%d" : "%Y-%m-%d %H:%M:%S";
        timestamps.emplace_back(FromStr(dates[i], fmt));
        open.emplace_back(static_cast<float>(data.at("open")[i]));
        close.emplace_back(static_cast<float>(data.at("close")[i]));
        high.emplace_back(static_cast<float>(data.at("high")[i]));
        low.emplace_back(static_cast<float>(data.at("low")[i]));
        volume.emplace_back(static_cast<int64_t>(data.at("volume")[i]));
    }

    if (timestamps.empty()) {
        WARN("BuildDataFrameFromMap: empty dates");
        return;
    }

    size_t numRows = timestamps.size();
    Vector<uint32_t> indexes(numRows);
    std::iota(indexes.begin(), indexes.end(), 1);

    df.load_index(std::move(indexes));
    df.load_column(header[0].c_str(), std::move(timestamps));
    df.load_column(header[1].c_str(), std::move(open));
    df.load_column(header[2].c_str(), std::move(close));
    df.load_column(header[3].c_str(), std::move(high));
    df.load_column(header[4].c_str(), std::move(low));
    df.load_column(header[5].c_str(), std::move(volume));
}

// ============ 订单撮合 ============

TradeReport HistorySimulationBase::OrderMatch(const Order& order, const QuoteInfo& quote) {
    TradeReport report;
    report._time = quote._time;
    report._quantity = order._volume;
    report._side = order._side;
    report._flag = order._flag;

    double fillPrice = order._price;
    if (_slippageModel && order._price > 0) {
        SlippageContext ctx{order, quote, order._price};
        double slip = _slippageModel->calculate(ctx);
        if (order._side == 0) {
            fillPrice = order._price + slip;
        } else {
            fillPrice = order._price - slip;
        }
    }
    report._price = fillPrice;
    report._trade_amount = order._volume * fillPrice;
    return report;
}

namespace {
    double calcCommission(const TradeReport& report, const Commission& comm) {
        if (comm._type != 0 || !comm._valid) return 0;
        double fee = report._trade_amount * comm._ration;
        return std::max((double)comm._min, fee);
    }

    double calcStampTax(const TradeReport& report, const Commission& comm) {
        if (report._side != 1 || comm._stamp == 0) return 0;
        return report._trade_amount * comm._stamp;
    }
}

void HistorySimulationBase::matchOrders(BacktestContext* context, symbol_t symbol) {
    auto* queue = context->getOrderQueue(symbol);
    if (!queue) return;

    const QuoteInfo* quote = context->getQuote(symbol);
    if (!quote) return;

    OrderInfo orderInfo;
    while (queue->pop(orderInfo)) {
        TradeReport report = OrderMatch(orderInfo._order->_order, *quote);
        const auto& order = orderInfo._order->_order;

        const Commission& comm = (order._side == 0) ? _buy : _sell;
        double commission = calcCommission(report, comm);
        double stampTax = calcStampTax(report, comm);
        double totalCost = commission + stampTax;

        double slipDiff = (order._flag == 1) ? 0.0 : (report._trade_amount - (order._volume * order._price));
        // 资金扣减/增加已在 OrderReport → adjustPosition 中通过 CapitalPool 处理
        // 旧代码的 releaseFunds 是空操作，已移除

        // 累加摩擦成本（佣金 + 印花税 + 滑点绝对值）
        context->addFrictionCost(totalCost + std::abs(slipDiff));

        context->addOrderReport(orderInfo._id, orderInfo._order);

        order_id id;
        id._id = orderInfo._id;
        OrderReport(context, id, report);
    }
}

// ============ 多线程回测 ============

run_id_t HistorySimulationBase::createBacktestContext(
    const String& strategy_name,
    const Set<symbol_t>& symbols,
    double initial_capital)
{
    // 确保数据已加载：首次加载 或 请求的 symbols 与已加载不一致时重新加载
    bool needReload = !_dataLoadSuccess;
    if (!needReload) {
        for (auto symbol : symbols) {
            if (_csvs.find(symbol) == _csvs.end()) {
                needReload = true;
                break;
            }
        }
    }
    INFO("[createBacktestContext] strategy={}, _dataLoadSuccess={}, _csvs.size={}, symbols={}, needReload={}",
         strategy_name, _dataLoadSuccess.load(), _csvs.size(), symbols.size(), needReload);
    if (needReload) {
        QuoteFilter filter;
        for (auto symbol : symbols) {
            filter._symbols.insert(get_symbol(symbol));
        }
        SetFilter(filter);
        INFO("[createBacktestContext] Calling Login with {} symbols in filter", filter._symbols.size());
        Login(AccountType::MAIN);
        INFO("[createBacktestContext] After Login: _csvs.size={}, _dataLoadSuccess={}", _csvs.size(), _dataLoadSuccess.load());
    }

    uint16_t runId = _nextRunId.fetch_add(1, std::memory_order_relaxed);

    auto broker = _server->GetBrokerSubSystem();
    if (broker) {
        broker->ClearHistoryTrades(runId);
    }

    auto context = std::make_unique<BacktestContext>(runId, strategy_name);
    context->setCapital(initial_capital);

    // 设置 CapitalPool 引用（资金统一管理）
    if (broker) {
        context->setCapitalPool(broker->GetCapitalPool());
        context->setStrategyName(strategy_name);
    }

    // 计算共同时间范围
    time_t maxStartTime = 0;
    time_t minEndTime = std::numeric_limits<time_t>::max();

    if (_hasBacktestTimeRange) {
        INFO("[Backtest] Configured time range: {} ~ {}",
             ToString(_backtestStartTime, "%Y-%m-%d"),
             ToString(_backtestEndTime, "%Y-%m-%d"));
    }

    for (auto symbol : symbols) {
        context->addSymbol(symbol);

        auto itr = _csvs.find(symbol);
        if (itr != _csvs.end()) {
            const auto& df = itr->second;
            const auto& header = _headers.at(symbol);

            if (df.get_index().size() > 0) {
                const auto& datetime = df.get_column<time_t>(header[0].c_str());

                time_t startTime = datetime[0];
                time_t endTime = datetime[datetime.size() - 1];

                if (startTime > maxStartTime) {
                    maxStartTime = startTime;
                }
                if (endTime < minEndTime) {
                    minEndTime = endTime;
                }

                INFO("Symbol {}: start={}, end={}",
                     get_symbol(symbol),
                     ToString(startTime, "%Y-%m-%d %H:%M:%S"),
                     ToString(endTime, "%Y-%m-%d %H:%M:%S"));
            }
        }
    }

    if (_backtestStartTime != 0) {
        maxStartTime = _backtestStartTime;
    }
    if (_backtestEndTime != 0) {
        minEndTime = _backtestEndTime;
    }
    context->setCommonStartTime(maxStartTime);
    context->setCommonEndTime(minEndTime);

    INFO("Common time range: start={}, end={}",
         ToString(maxStartTime, "%Y-%m-%d %H:%M:%S"),
         ToString(minEndTime, "%Y-%m-%d %H:%M:%S"));

    // 设置每个 symbol 的起始索引，并计算对齐后的共同 bar 数量
    size_t commonBars = std::numeric_limits<size_t>::max();
    for (auto symbol : symbols) {
        auto itr = _csvs.find(symbol);
        if (itr != _csvs.end()) {
            const auto& df = itr->second;
            const auto& header = _headers.at(symbol);
            const auto& datetime = df.get_column<time_t>(header[0].c_str());

            uint32_t startIndex = 0;
            for (uint32_t i = 0; i < datetime.size(); ++i) {
                if (datetime[i] >= maxStartTime) {
                    startIndex = i;
                    break;
                }
            }

            context->setCurIndex(symbol, startIndex);
            INFO("Symbol {} start index: {}", get_symbol(symbol), startIndex);

            // 计算该 symbol 在共同时间范围内的 bar 数量
            size_t symbolBars = (datetime.size() > startIndex) ? (datetime.size() - startIndex) : 0;
            if (symbolBars < commonBars) {
                commonBars = symbolBars;
            }
        } else {
            context->setCurIndex(symbol, 0);
            commonBars = 0;  // 没有数据的 symbol，共同 bar 数为 0
        }
    }

    // 使用对齐后的共同 bar 数量，而不是第一个 symbol 的 bar 数
    if (commonBars == std::numeric_limits<size_t>::max()) {
        commonBars = 0;  // 没有有效数据
    }
    context->setTotalBars(commonBars);
    context->reserveDailySnapshots(commonBars);

    _backtestContexts.emplace(runId, std::move(context));

    INFO("Created backtest context: runId={}, strategy={}, symbols={}, initialCapital={}",
         runId, strategy_name, symbols.size(), initial_capital);

    return runId;
}

BacktestContext* HistorySimulationBase::getBacktestContext(run_id_t run_id) {
    BacktestContext* ctx = nullptr;
    _backtestContexts.visit(run_id, [&ctx](auto& item) {
        ctx = item.second.get();
    });
    return ctx;
}

const BacktestContext* HistorySimulationBase::getBacktestContext(run_id_t run_id) const {
    const BacktestContext* ctx = nullptr;
    _backtestContexts.visit(run_id, [&ctx](auto& item) {
        ctx = item.second.get();
    });
    return ctx;
}

void HistorySimulationBase::destroyBacktestContext(run_id_t run_id) {
    _backtestContexts.erase(run_id);
}

/**
 * @brief 将时间戳截断到日期（本地时区）
 */
static inline time_t DateOnly(time_t ts) {
    struct tm tm_val;
#ifdef _WIN32
    localtime_s(&tm_val, &ts);
#else
    localtime_r(&ts, &tm_val);
#endif
    tm_val.tm_hour = 0;
    tm_val.tm_min = 0;
    tm_val.tm_sec = 0;
    return mktime(&tm_val);
}

bool HistorySimulationBase::stepForward(BacktestContext* context) {
    auto symbols = context->getSymbols();
    if (symbols.empty()) return false;

    bool anyMoreData = false;
    bool anyFinished = false;

    std::shared_lock<std::shared_mutex> dataLock(_dataMutex);

    // === 找出所有symbol当前时间的最小值（最早时间） ===
    Map<symbol_t, QuoteInfo> quotes;
    time_t minTime = std::numeric_limits<time_t>::max();
    for (auto symbol : symbols) {
        auto itr = _csvs.find(symbol);
        if (itr == _csvs.end()) {
            WARN("[HistorySimulationBase] symbol {} not found in _csvs!", get_symbol(symbol));
            continue;
        }

        const auto& df = itr->second;
        const auto& header = _headers.at(symbol);
        if (header.empty()) continue;

        const auto& datetime = df.get_column<time_t>(header[0].c_str());
        uint32_t curIndex = context->getCurIndex(symbol);
        if (curIndex < datetime.size()) {
            if (datetime[curIndex] < minTime)
                minTime = datetime[curIndex];
        }
        else {
            // 任何 symbol 数据用完，立即结束整个回测
            context->setFinished(true);
            anyFinished = true;
            break;
        }
        const auto& open = df.get_column<float>(header[1].c_str());
        const auto& close = df.get_column<float>(header[2].c_str());
        const auto& high = df.get_column<float>(header[3].c_str());
        const auto& low = df.get_column<float>(header[4].c_str());
        const auto& volume = df.get_column<int64_t>(header[5].c_str());

        auto org_itr = _org_csvs.find(symbol);
        QuoteInfo info;
        info._symbol = symbol;
        info._volume = volume[curIndex];
        info._time = datetime[curIndex];

        if (org_itr != _org_csvs.end() && !_org_headers.at(symbol).empty()) {
            const auto& org_df = org_itr->second;
            const auto& org_header = _org_headers.at(symbol);
            uint32_t org_index = curIndex;
            if (org_index >= org_df.get_index().size()) {
                org_index = org_df.get_index().size() - 1;
            }
            info._open = org_df.get_column<float>(org_header[1].c_str())[org_index];
            info._close = org_df.get_column<float>(org_header[2].c_str())[org_index];
            info._high = org_df.get_column<float>(org_header[3].c_str())[org_index];
            info._low = df.get_column<float>(org_header[4].c_str())[org_index];
        }
        else {
            info._open = open[curIndex];
            info._close = close[curIndex];
            info._high = high[curIndex];
            info._low = low[curIndex];
        }
        quotes[symbol] = std::move(info);
    }
    // 推进时间最小的symbol
    for (auto& [symbol, quote] : quotes) {
        context->setQuote(symbol, quote);
        if (quote._time == minTime) {
            context->incrementCurIndex(symbol);
            anyMoreData = true;
        }
        //INFO("incrementCurIndex {}, time: {}", symbol, ToString(quote._time));
    }

    //INFO("---------------------------------------");
    dataLock.unlock();

    // 如果任何 symbol 数据用完，立即返回 false 结束回测
    if (anyFinished) {
        return false;
    }

    // === 跨日检测 ===
    for (auto symbol : symbols) {
        const QuoteInfo* q = context->getQuote(symbol);
        if (q && q->_time > 0) {
            time_t lastDay = context->getLastTradeDay();
            time_t curDay = DateOnly(q->_time);
            if (curDay != lastDay && lastDay != 0) {
                context->onDayChange();
            }
            context->setLastTradeDay(curDay);
        }
    }

    // === 计算调整系数 ===
    for (auto symbol : symbols) {
        auto hfq_itr = _csvs.find(symbol);
        auto org_itr = _org_csvs.find(symbol);
        auto org_header_it = _org_headers.find(symbol);

        if (hfq_itr == _csvs.end() || org_itr == _org_csvs.end() ||
            org_header_it == _org_headers.end() || org_header_it->second.empty()) {
            context->setCurrentAdjRatio(symbol, 1.0);
            continue;
        }

        const auto& hfq_df = hfq_itr->second;
        const auto& hfq_header = _headers.at(symbol);
        const auto& org_df = org_itr->second;
        const auto& org_header = org_header_it->second;

        auto curIndex = context->getCurIndex(symbol);
        uint32_t priceIndex = (curIndex > 0) ? curIndex - 1 : 0;

        if (priceIndex < hfq_df.get_index().size() && priceIndex < org_df.get_index().size()) {
            double hfqClose = hfq_df.get_column<float>(hfq_header[2].c_str())[priceIndex];
            double origClose = org_df.get_column<float>(org_header[2].c_str())[priceIndex];
            double ratio = (origClose > 0.0 && hfqClose > 0.0) ? hfqClose / origClose : 1.0;
            context->setCurrentAdjRatio(symbol, ratio);
        } else {
            context->setCurrentAdjRatio(symbol, 1.0);
        }
    }

    // === 订单撮合 ===
    for (auto symbol : symbols) {
        matchOrders(context, symbol);
    }

    // === 每日快照 ===
    {
        double totalMarketValue = 0.0;
        Map<symbol_t, double> assetValues;

        for (auto symbol : symbols) {
            int64_t position = context->getPosition(symbol);
            if (position <= 0) {
                assetValues[symbol] = 0.0;
                continue;
            }

            auto itr = _csvs.find(symbol);
            if (itr == _csvs.end()) continue;
            const auto& header_it = _headers.find(symbol);
            if (header_it == _headers.end() || header_it->second.empty()) continue;

            const auto& df = itr->second;
            const auto& header = header_it->second;
            auto curIndex = context->getCurIndex(symbol);
            uint32_t priceIndex = (curIndex > 0) ? curIndex - 1 : 0;
            if (priceIndex < df.get_index().size()) {
                double hfqClose = df.get_column<float>(header[2].c_str())[priceIndex];
                double ratio = context->getCurrentAdjRatio(symbol);
                double assetValue = position * hfqClose / ratio;
                assetValues[symbol] = assetValue;
                totalMarketValue += assetValue;
            }
        }

        double currentAvailable = context->getAvailableFunds();
        double totalEquity = currentAvailable + totalMarketValue;

        const QuoteInfo* firstQuote = nullptr;
        for (auto symbol : symbols) {
            const QuoteInfo* q = context->getQuote(symbol);
            if (q && q->_time > 0) {
                firstQuote = q;
                break;
            }
        }
        if (firstQuote) {
            context->recordDailySnapshot(firstQuote->_time, totalEquity);
            context->recordDailyAssetSnapshot(firstQuote->_time, assetValues);

            // 记录快照后，计算当日收益率并更新 CUSUM 检测
            size_t snapshotCount = context->dailySnapshotCount();
            if (snapshotCount > 1) {
                double prevEquity = context->getPortfolioValues()[snapshotCount - 2];
                if (prevEquity > 1e-6) {
                    double daily_return = (totalEquity - prevEquity) / prevEquity;
                    context->updateCUSUM(daily_return);
                }
            }
        } else {
            WARN("[HistorySimulationBase] No valid quote found for any symbol, skipping daily snapshot");
        }
    }

    // === 更新进度 ===
    uint32_t maxIndex = 0;
    for (auto symbol : symbols) {
        uint32_t idx = context->getCurIndex(symbol);
        if (idx > maxIndex) maxIndex = idx;
    }
    if (context->getTotalBars() > 0) {
        double progress = (double)maxIndex / context->getTotalBars();
        if (progress > 1.0) progress = 1.0;
        context->setTotalBars(context->getTotalBars());
    }

    return anyMoreData;
}

QuoteInfo HistorySimulationBase::GetQuote(symbol_t symbol, run_id_t run_id) {
    BacktestContext* ctx = nullptr;
    _backtestContexts.visit(run_id, [&ctx](auto& item) {
        ctx = item.second.get();
    });

    if (ctx) {
        const QuoteInfo* quote = ctx->getQuote(symbol);
        if (quote) {
            return *quote;
        }
    }

    QuoteInfo empty;
    empty._symbol = symbol;
    empty._time = 0;
    return empty;
}

double HistorySimulationBase::GetPrimitivePrice(symbol_t symbol, uint32_t index) const {
    auto org_itr = _org_csvs.find(symbol);
    if (org_itr == _org_csvs.end()) {
        return GetAdjPrice(symbol, index);
    }
    auto& org_df = org_itr->second;
    auto& org_header = _org_headers.at(symbol);
    if (org_header.empty()) {
        return GetAdjPrice(symbol, index);
    }

    auto& org_close = org_df.get_column<float>(org_header[2].c_str());
    if (index >= org_close.size()) {
        index = org_close.size() - 1;
    }
    return org_close[index];
}

double HistorySimulationBase::GetAdjPrice(symbol_t symbol, uint32_t index) const {
    auto itr = _csvs.find(symbol);
    if (itr == _csvs.end()) {
        return 0.0;
    }
    auto& df = itr->second;
    auto& header = _headers.at(symbol);
    if (header.empty()) {
        return 0.0;
    }
    auto& close = df.get_column<float>(header[2].c_str());
    if (index >= close.size()) {
        index = close.size() - 1;
    }
    return close[index];
}

int64_t HistorySimulationBase::GetPositionQuantity(symbol_t symbol) const {
    int64_t position = 0;
    _backtestContexts.visit_all([&position, &symbol](const auto& item) {
        position = item.second->getPosition(symbol);
    });
    return position;
}

QuoteInfo HistorySimulationBase::GetQuote(symbol_t symbol) {
    QuoteInfo empty;
    empty._symbol = symbol;
    empty._time = 0;
    return empty;
}

double HistorySimulationBase::Progress(const String& strategy) {
    BacktestContext* ctx = nullptr;
    _backtestContexts.visit_all([&ctx, &strategy](auto& item) {
        if (item.second->getStrategyName() == strategy) {
            ctx = item.second.get();
        }
    });

    if (ctx) {
        return ctx->getProgress();
    }
    return 0.0;
}

bool HistorySimulationBase::HasBacktestContext(const String& strategy) const {
    bool found = false;
    _backtestContexts.visit_all([&found, &strategy](const auto& item) {
        if (item.second->getStrategyName() == strategy) {
            found = true;
        }
    });
    return found;
}

// ============ 合约信息查询 ============

bool HistorySimulationBase::GetAllStockSymbols(List<SymbolInfo>& symbols) {
    String csv_path = _org_path + "/symbol_market.csv";

    if (!std::filesystem::exists(csv_path)) {
        WARN("{} not exist, running script to generate", csv_path);
        String cmd = "python " + _org_path + "/../tools/run_task.py 1";
        if (!RunCommand(cmd)) {
            FATAL("Failed to run script to generate symbol_market.csv");
            return false;
        }
    }

    try {
        io::CSVReader<3> reader(csv_path);
        reader.read_header(io::ignore_extra_column, "代码", "交易所", "name");
        std::string code, exch, name;
        while (reader.read_row(code, exch, name)) {
            SymbolInfo info;
            info._code = code;
            info._name = name;
            if (exch == "SH") {
                info._exchange = MT_Shanghai;
            } else if (exch == "SZ") {
                info._exchange = MT_Shenzhen;
            } else if (exch == "BJ") {
                info._exchange = MT_Beijing;
            } else {
                WARN("{}: Unknown exchange {}", code, exch);
                continue;
            }
            info._type = static_cast<char>(ContractType::AStock);
            symbols.push_back(info);
        }
        INFO("Loaded {} stock symbols from {}", symbols.size(), csv_path);
        return true;
    } catch (const std::exception& e) {
        FATAL("Failed to load {}: {}", csv_path, e.what());
        return false;
    }
}

bool HistorySimulationBase::GetAllFundSymbols(List<SymbolInfo>& symbols) {
    String csv_path = _org_path + "/fund_market.csv";

    if (!std::filesystem::exists(csv_path)) {
        WARN("{} not exist, running script to generate", csv_path);
        String cmd = "python " + _org_path + "/../tools/run_task.py 2";
        if (!RunCommand(cmd)) {
            FATAL("Failed to run script to generate fund_market.csv");
            return false;
        }
    }

    try {
        io::CSVReader<3> reader(csv_path);
        reader.read_header(io::ignore_extra_column, "code", "name", "type");
        std::string code, name, type;
        while (reader.read_row(code, name, type)) {
            SymbolInfo info;
            info._code = code.substr(2);
            info._name = name;
            if (code.substr(0, 2) == "sh") {
                info._exchange = MT_Shanghai;
            } else if (code.substr(0, 2) == "sz") {
                info._exchange = MT_Shenzhen;
            } else {
                continue;
            }
            if (type == "ETF 基金") {
                info._type = static_cast<char>(ContractType::ETF);
            } else if (type == "LOF 基金") {
                info._type = static_cast<char>(ContractType::LOF);
            } else {
                info._type = static_cast<char>(ContractType::ETF);
            }
            symbols.push_back(info);
        }
        INFO("Loaded {} fund symbols from {}", symbols.size(), csv_path);
        return true;
    } catch (const std::exception& e) {
        FATAL("Failed to load {}: {}", csv_path, e.what());
        return false;
    }
}

bool HistorySimulationBase::GetAllETFSymbols(List<SymbolInfo>& symbols) {
    // 场内ETF数据目录: data/etf_org 和 data/etf_hfq
    String csv_path = _org_path + "/etf_market.csv";

    if (!std::filesystem::exists(csv_path)) {
        WARN("{} not exist, trying to scan data directory", csv_path);
        // 如果 CSV 不存在，尝试从数据目录扫描
        String dataDir = _org_path + "/etf_org";
        if (!std::filesystem::exists(dataDir)) {
            WARN("ETF data directory not exist: {}", dataDir);
            return false;
        }

        // 扫描目录中的 CSV 文件
        for (auto& entry : std::filesystem::directory_iterator(dataDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".csv") {
                SymbolInfo info;
                info._code = entry.path().stem().string();
                info._name = info._code;  // 暂无名称，使用代码

                // 根据代码判断交易所：6位数字代码
                if (info._code.size() >= 8) {
                    String prefix = info._code.substr(0, 2);
                    if (prefix == "sh") {
                        info._exchange = MT_Shanghai;
                    } else if (prefix == "sz") {
                        info._exchange = MT_Shenzhen;
                    } else {
                        continue;
                    }
                } else {
                    // 纯6位数字代码，根据首位判断
                    char firstDigit = info._code[0];
                    if (firstDigit == '5' || firstDigit == '6') {
                        info._exchange = MT_Shanghai;
                    } else if (firstDigit == '0' || firstDigit == '1' || firstDigit == '3') {
                        info._exchange = MT_Shenzhen;
                    } else {
                        continue;
                    }
                }

                info._type = static_cast<char>(ContractType::ETF);
                symbols.push_back(info);
            }
        }

        INFO("Scanned {} ETF symbols from directory {}", symbols.size(), dataDir);
        return !symbols.empty();
    }

    try {
        io::CSVReader<3> reader(csv_path);
        reader.read_header(io::ignore_extra_column, "code", "name", "exchange");
        std::string code, name, exch;
        while (reader.read_row(code, name, exch)) {
            SymbolInfo info;
            info._code = code;
            info._name = name;

            if (exch == "SH" || exch == "sh") {
                info._exchange = MT_Shanghai;
            } else if (exch == "SZ" || exch == "sz") {
                info._exchange = MT_Shenzhen;
            } else {
                WARN("{}: Unknown exchange {}", code, exch);
                continue;
            }

            info._type = static_cast<char>(ContractType::ETF);
            symbols.push_back(info);
        }
        INFO("Loaded {} ETF symbols from {}", symbols.size(), csv_path);
        return true;
    } catch (const std::exception& e) {
        FATAL("Failed to load {}: {}", csv_path, e.what());
        return false;
    }
}

bool HistorySimulationBase::GetAllOptionSymbols(List<SymbolInfo>& symbols) {
    String csv_path = _org_path + "/option_market.csv";

    if (!std::filesystem::exists(csv_path)) {
        WARN("{} not exist, running script to generate", csv_path);
        String cmd = "python " + _org_path + "/../tools/run_task.py 3";
        if (!RunCommand(cmd)) {
            FATAL("Failed to run script to generate option_market.csv");
            return false;
        }
    }

    try {
        io::CSVReader<6> reader(csv_path);
        reader.read_header(io::ignore_extra_column, "交易所 ID", "合约 ID", "合约名称", "最后交易日", "交割日", "行权价");
        std::string exch, code, name, expire, delivery;
        double strike;
        while (reader.read_row(exch, code, name, expire, delivery, strike)) {
            SymbolInfo info;
            info._code = code;
            info._name = name;
            info._expireDate = expire;
            info._deliveryDate = delivery;
            info._strike = static_cast<float>(strike);

            if (exch == "SZSE") {
                info._exchange = MT_Shenzhen;
            } else if (exch == "SSE") {
                info._exchange = MT_Shanghai;
            } else {
                continue;
            }

            bool isPut = (name.find("沽") != String::npos) ||
                        (name.find("P") != String::npos && name.find("P") > 3);
            if (isPut) {
                info._type = static_cast<char>(ContractType::AmericanOption);
            } else {
                info._type = static_cast<char>(ContractType::AmericanOption) | (1 << 7);
            }

            symbols.push_back(info);
        }
        INFO("Loaded {} option symbols from {}", symbols.size(), csv_path);
        return true;
    } catch (const std::exception& e) {
        FATAL("Failed to load {}: {}", csv_path, e.what());
        return false;
    }
}

SymbolInfo HistorySimulationBase::GetSymbolInfo(const String& code) {
    SymbolInfo info;

    List<SymbolInfo> stocks;
    if (GetAllStockSymbols(stocks)) {
        for (const auto& s : stocks) {
            if (s._code == code) return s;
        }
    }

    List<SymbolInfo> funds;
    if (GetAllFundSymbols(funds)) {
        for (const auto& s : funds) {
            if (s._code == code) return s;
        }
    }

    List<SymbolInfo> options;
    if (GetAllOptionSymbols(options)) {
        for (const auto& s : options) {
            if (s._code == code) return s;
        }
    }

    return info;
}

void HistorySimulationBase::RefreshSymbolList() {
}

// ============ 工具方法 ============

void HistorySimulationBase::Clear() {
    _csvs.clear();
    _org_csvs.clear();
    _headers.clear();
    _org_headers.clear();
    _reports.clear();
    _cur_id = 0;

    // 不销毁回测上下文，由外部管理
}

void HistorySimulationBase::SetBacktestTimeRange(time_t start, time_t end) {
    _hasBacktestTimeRange = true;
    _backtestStartTime = start;
    _backtestEndTime = end;
}

bool HistorySimulationBase::HasBacktestTimeRange() const {
    return _hasBacktestTimeRange;
}

time_t HistorySimulationBase::GetBacktestStartTime() const {
    return _backtestStartTime;
}

time_t HistorySimulationBase::GetBacktestEndTime() const {
    return _backtestEndTime;
}
