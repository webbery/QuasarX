#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Bridge/PositionEvent.h"
#include "Bridge/exchange.h"
#include "BrokerSubSystem.h"
#include "Util/FinanceDB.h"
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
#include <shared_mutex>
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

namespace {

/// 删掉缓存里所有不在 keep 集合中的键（std::map::erase(it) 返回下一个迭代器）
template <typename T>
void eraseIfUnused(Map<symbol_t, T>& cache, const Set<symbol_t>& keep) {
    for (auto itr = cache.begin(); itr != cache.end();) {
        if (keep.count(itr->first)) {
            ++itr;
        } else {
            itr = cache.erase(itr);
        }
    }
}

}  // namespace

bool HistorySimulationBase::Login(AccountType t) {
    // 重载会改写 _csvs/_org_csvs，必须与 stepForward / createBacktestContext 的
    // 共享读互斥：日终回放每次都会 forceReload，两个策略并发时另一个可能正在读缓存，
    // 不互斥就会读到写了一半的 map。
    std::unique_lock<std::shared_mutex> dataLock(_dataMutex);
    _finish = false;

    // 只刷新本次请求的标的，绝不整表清空。_csvs/_org_csvs 是交易所级共享缓存，
    // 日终回放每个策略每次执行都会 forceReload 走到这里；整表清空会把并发跑的
    // 另一个策略的标的连带抹掉，对方 stepForward 查不到自己的 symbol，回放窗口
    // 塌缩成 0 根 bar
    DropCachedSymbols(_filter._symbols);
    EvictUnusedSymbols();

    // 每轮回放的状态，与行情缓存无关，照旧重置
    _reports.clear();
    _cur_id = 0;

    LoadFilteredSymbols();

    // 清除加载日期范围（一次性使用，避免影响后续 Login 调用）
    _loadStartDate.clear();
    _loadEndDate.clear();

    OnDataLoaded();
    _dataLoadSuccess = true;
    return true;
}

void HistorySimulationBase::DropCachedSymbols(const Set<String>& codes) {
    for (auto& code : codes) {
        auto symbol = to_symbol(code);
        _csvs.erase(symbol);
        _org_csvs.erase(symbol);
    }
}

void HistorySimulationBase::EvictUnusedSymbols() {
    // 保留：本次请求的标的 + 所有活跃回测上下文正在使用的标的。
    // 一次 Login 只扫描一次，开销可以接受；换来的是缓存体积有界，
    // 又不会误删并发策略正在跑的数据。
    Set<symbol_t> inUse;
    for (auto& code : _filter._symbols) {
        inUse.insert(to_symbol(code));
    }
    _backtestContexts.visit_all([&inUse](const auto& item) {
        for (auto symbol : item.second->getSymbols()) {
            inUse.insert(symbol);
        }
    });

    eraseIfUnused(_csvs, inUse);
    eraseIfUnused(_org_csvs, inUse);
}

void HistorySimulationBase::LoadFilteredSymbols() {
    for (auto& code : _filter._symbols) {
        if (!LoadData(code)) {
            WARN("Failed to load data for '{}'", code);
        }
    }
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
    order_id id;
    id._id = ++_cur_id;
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

    // 信用动作判定：优先用委托自带的 _creditType（ImmediateTiming 按持仓打标），
    // 委托没带类型时按持仓与策略两融配置兜底推断。
    //   只有 ShortSell 允许开出负仓 —— 普通卖单在无持仓时仍然被拒绝，
    //   现有多头回测的行为完全不变。
    double marginRatio = 1.0;
    bool strategyAllowShort = false;
    if (_server) {
        if (auto* broker = _server->GetBrokerSubSystem()) {
            if (auto* capital = broker->GetStrategyCapital(context->getStrategyName())) {
                const auto& cfg = capital->getMarginConfig();
                marginRatio = cfg._initialMarginRatio;
                strategyAllowShort = cfg._allowShort;
            }
        }
    }

    CreditAction credit = CreditAction::None;
    switch (order._creditType) {
    case CreditOrderType::ShortSell:
        credit = CreditAction::ShortSell;
        break;
    case CreditOrderType::CoverShort:
        credit = CreditAction::ShortCover;
        break;
    case CreditOrderType::MarginBuy:
        credit = CreditAction::MarginBuy;
        break;
    default:
        break;
    }
    if (credit == CreditAction::None) {
        int64_t pos = context->getPosition(order._symbol);
        if (order._side == 1) {
            // 卖出：无多仓且策略允许做空 → 融券卖出
            if (pos <= 0 && strategyAllowShort) {
                credit = CreditAction::ShortSell;
            }
        } else {
            // 买入：持空仓 → 买券还券；否则按保证金比例判定是否融资买入
            if (pos < 0) {
                credit = CreditAction::ShortCover;
            } else if (marginRatio < 1.0) {
                credit = CreditAction::MarginBuy;
            }
        }
    }

    context->adjustPosition(orderCtx->_order._symbol, delta, report._price, credit, marginRatio);

    // 成交计数：回测里唯一的成交落点（零成交自检依赖它，见 ComputeBacktestMetrics）
    context->recordFill();

    orderCtx->_success.store(true);
    orderCtx->_flag.store(true);

    // 回测模式：同步记录交易到 BrokerSubSystem（消除 worker 线程竞态）
    if (_server && orderCtx->_running_type == static_cast<uint8_t>(RuningType::Backtest)) {
        auto* broker = _server->GetBrokerSubSystem();
        if (broker) {
            broker->RecordTrade(*orderCtx);
        }
    }

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

// ============ 数据构建 ============

void HistorySimulationBase::BuildOHLCVDataFromMap(
    const Map<String, Vector<double>>& data,
    const Vector<String>& dates,
    OHLCVData& out)
{
    static const char* kFields[] = {"open", "close", "high", "low", "volume"};

    // 字段缺失/长度不足时先收敛到安全行数：下面按下标逐列取值，越界不会抛异常，
    // 只会读到脏内存或崩溃
    size_t rows = dates.size();
    for (const char* field : kFields) {
        auto it = data.find(field);
        if (it == data.end()) {
            WARN("BuildOHLCVDataFromMap: 缺少字段 '{}'，放弃本次构建", field);
            out = OHLCVData{};
            return;
        }
        rows = std::min(rows, it->second.size());
    }
    if (rows < dates.size()) {
        WARN("BuildOHLCVDataFromMap: 字段长度({}) < 日期长度({})，按 {} 行截断",
             rows, dates.size(), rows);
    }

    out._datetime.reserve(rows);
    out._open.reserve(rows);
    out._close.reserve(rows);
    out._high.reserve(rows);
    out._low.reserve(rows);
    out._volume.reserve(rows);

    // 同一时间戳只保留一根 bar（后出现的值覆盖先出现的）。
    //
    // 重复 bar 的破坏力不止"多算一根"：stepForward 只推进当前时间等于 minTime 的
    // 标的，某标的在同一天有 N 行时它会原地走 N 步，其余标的停在旧价，
    // 价差/β/成交量全部来自不同交易日的拼接。日志里表现为同一天反复出现、
    // 且两腿比例系数在同一根 bar 内剧烈漂移（实测 β 3.59→1.82）。
    Map<time_t, size_t> seen;          // 时间戳 → 输出下标
    size_t dupMerged = 0;
    String firstDup;
    for (size_t i = 0; i < rows; ++i) {
        const char* fmt = (dates[i].size() <= 10) ? "%Y-%m-%d" : "%Y-%m-%d %H:%M:%S";
        time_t ts = FromStr(dates[i], fmt);

        auto hit = seen.find(ts);
        if (hit != seen.end()) {
            size_t pos = hit->second;
            out._open[pos] = data.at("open")[i];
            out._close[pos] = data.at("close")[i];
            out._high[pos] = data.at("high")[i];
            out._low[pos] = data.at("low")[i];
            out._volume[pos] = static_cast<int64_t>(data.at("volume")[i]);
            if (dupMerged == 0) firstDup = dates[i];
            ++dupMerged;
            continue;
        }

        seen.emplace(ts, out._datetime.size());
        out._datetime.emplace_back(ts);
        out._open.emplace_back(data.at("open")[i]);
        out._close.emplace_back(data.at("close")[i]);
        out._high.emplace_back(data.at("high")[i]);
        out._low.emplace_back(data.at("low")[i]);
        out._volume.emplace_back(static_cast<int64_t>(data.at("volume")[i]));
    }

    if (dupMerged > 0) {
        WARN("BuildOHLCVDataFromMap: 合并 {} 条重复时间戳（首次 {}），"
             "原始 {} 行 → {} 根 bar。请检查数据源是否重复导入",
             dupMerged, firstDup, rows, out.size());
    }

    if (out._datetime.empty()) {
        WARN("BuildOHLCVDataFromMap: empty dates");
        return;
    }

    // stepForward 依赖时间戳严格递增。乱序数据不会被上面的去重合并，
    // 这里显式排序一次，避免整条回测时间轴错乱
    if (!std::is_sorted(out._datetime.begin(), out._datetime.end())) {
        WARN("BuildOHLCVDataFromMap: 时间戳非递增，已按时间排序");
        Vector<size_t> order(out.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&](size_t a, size_t b) { return out._datetime[a] < out._datetime[b]; });

        OHLCVData sorted;
        sorted._datetime.reserve(order.size());
        sorted._open.reserve(order.size());
        sorted._close.reserve(order.size());
        sorted._high.reserve(order.size());
        sorted._low.reserve(order.size());
        sorted._volume.reserve(order.size());
        for (size_t idx : order) {
            sorted._datetime.emplace_back(out._datetime[idx]);
            sorted._open.emplace_back(out._open[idx]);
            sorted._close.emplace_back(out._close[idx]);
            sorted._high.emplace_back(out._high[idx]);
            sorted._low.emplace_back(out._low[idx]);
            sorted._volume.emplace_back(out._volume[idx]);
        }
        out = std::move(sorted);
    }
}

bool HistorySimulationBase::ValidateSeriesAlignment(symbol_t symbol) const {
    auto adjIt = _csvs.find(symbol);
    auto orgIt = _org_csvs.find(symbol);
    if (adjIt == _csvs.end() || orgIt == _org_csvs.end()) {
        WARN("[{}] 复权/原始序列缺失（adj={}, org={}），跳过该标的",
             get_symbol(symbol), adjIt != _csvs.end(), orgIt != _org_csvs.end());
        return false;
    }

    const auto& adj = adjIt->second;
    const auto& org = orgIt->second;

    if (adj.size() != org.size()) {
        WARN("[{}] 复权/原始序列长度不一致（adj={} org={}）："
             "stepForward 用同一个 curIndex 取两份序列，会静默用错位的价格撮合。"
             "请检查数据源是否在同一天有重复或缺失行",
             get_symbol(symbol), adj.size(), org.size());
        return false;
    }

    for (size_t i = 0; i < adj.size(); ++i) {
        if (adj._datetime[i] != org._datetime[i]) {
            WARN("[{}] 第 {} 根 bar 时间戳不一致（adj={} org={}），撮合价会错位",
                 get_symbol(symbol), i,
                 ToString(adj._datetime[i]), ToString(org._datetime[i]));
            return false;
        }
    }

    return true;
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
        // 资金扣减/增加已在 OrderReport → adjustPosition 中通过账本处理
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
    double initial_capital,
    const String& load_start_date,
    const String& load_end_date,
    bool forceReload)
{
    // 确保数据已加载：首次加载 或 请求的 symbols 与已加载不一致时重新加载。
    // forceReload 用于日终回放：DB 才是事实来源，缓存里的最后一天可能已经过期
    // （simulate/bar 写入新 bar 后没有失效缓存），不重载就会把整轮回放跑在旧窗口上。
    bool needReload = forceReload || !_dataLoadSuccess;
    if (!needReload) {
        for (auto symbol : symbols) {
            auto itr = _csvs.find(symbol);
            if (itr == _csvs.end() || itr->second.empty()) {
                needReload = true;
                break;
            }
        }
    }
    // 指定了加载日期范围时，强制重新加载以使用最新数据
    if (!load_start_date.empty() || !load_end_date.empty()) {
        needReload = true;
        SetLoadDateRange(load_start_date, load_end_date);
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

    // 设置账本引用（资金统一管理）
    if (broker) {
        auto* pool = broker->GetLedger();
        context->setLedger(pool);
        context->setStrategyName(strategy_name);
        // 仅 Backtest/Simulation 在这里注册资金到账本：
        //   - Real 模式资金由券商账户管理，不进本地账本
        //   - Shadow 模式只记录日志不下单，无需资金
        //   - BackTestHandler 路径（HTTP /v0/backtest）已 allocate 过，hasStrategy 跳过避免覆盖累积的资金
        auto mode = _server->GetRunningMode();
        if ((mode == RuningType::Backtest || mode == RuningType::Simualtion)
            && pool && !pool->HasStrategy(strategy_name)) {
            // 注册由本次调用建立 → 归属本上下文，析构时自动归还（RAII）
            if (pool->Allocate(strategy_name, initial_capital)) {
                context->setCapitalReserved(true);
            } else {
                WARN("[createBacktestContext] Failed to allocate {:.0f} capital for '{}'",
                     initial_capital, strategy_name);
            }
        }
    }

    // 计算共同时间范围
    //
    // 下面的对齐计算（startIndex / commonBars / tradingStartEpochs）直接读 _csvs
    // 里的 bar 序列，必须与 Login 的重载互斥：否则并发跑的另一个策略正在重载缓存时，
    // 这里会读到空条目，commonBars 塌成 0，整轮回放零根 bar，决策全丢。
    std::shared_lock<std::shared_mutex> dataLock(_dataMutex);

    time_t maxStartTime = 0;
    time_t minEndTime = std::numeric_limits<time_t>::max();

    if (_hasBacktestTimeRange) {
        // backtest.start 表示「交易起点」而非「数据窗截断」：数据仍从自然起点加载，
        // 让特征节点有足够历史预热，只是在前 N 个 epoch 内跳过 Signal/Execution/Portfolio
        // 节点（N = 数据窗起点到 backtest.start 的 bar 数，见下方 tradingStartEpochs）。
        INFO("[Backtest] Configured range: trading {} ~ {} (data window keeps full history for warmup)",
             ToString(_backtestStartTime, "%Y-%m-%d"),
             ToString(_backtestEndTime, "%Y-%m-%d"));
    }

    for (auto symbol : symbols) {
        context->addSymbol(symbol);

        auto itr = _csvs.find(symbol);
        if (itr != _csvs.end()) {
            const auto& data = itr->second;

            if (!data.empty()) {
                time_t startTime = data._datetime[0];
                time_t endTime = data._datetime.back();

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
    // 交易起点偏移：数据窗起点 → backtest.start 之间的 bar 数（取所有标的最晚值）。
    // 数据窗本身不截断，特征节点照常从窗口起点开始预热；
    // 该偏移只会作为 warmup 下限，让 Signal/Execution/Portfolio 在 start 之前不执行。
    size_t tradingStartEpochs = 0;
    for (auto symbol : symbols) {
        auto itr = _csvs.find(symbol);
        if (itr != _csvs.end()) {
            const auto& data = itr->second;

            uint32_t startIndex = 0;
            for (uint32_t i = 0; i < data.size(); ++i) {
                if (data._datetime[i] >= maxStartTime) {
                    startIndex = i;
                    break;
                }
            }

            context->setCurIndex(symbol, startIndex);
            DEBUG_INFO("Symbol {} start index: {}", get_symbol(symbol), startIndex);

            if (_backtestStartTime != 0) {
                size_t off = data.size() - startIndex;  // 默认：start 之后无数据 → 全程不交易
                for (size_t i = startIndex; i < data.size(); ++i) {
                    if (data._datetime[i] >= _backtestStartTime) {
                        off = i - startIndex;
                        break;
                    }
                }
                tradingStartEpochs = std::max(tradingStartEpochs, off);
            }

            size_t symbolBars = (data.size() > startIndex) ? (data.size() - startIndex) : 0;
            if (symbolBars < commonBars) {
                commonBars = symbolBars;
            }
        } else {
            context->setCurIndex(symbol, 0);
            commonBars = 0;  // 没有数据的 symbol，共同 bar 数为 0
        }
    }

    // 对齐计算到此结束，放开共享锁：后面只写 context，不再读共享缓存
    dataLock.unlock();

    // 交易起点偏移作为 warmup 下限写入 context（AgentSubSystem 会与推断 warmup 取 max）
    context->setWarmupEpochsHint(static_cast<int>(tradingStartEpochs));
    if (tradingStartEpochs > 0) {
        INFO("[Backtest] Trading starts after {} warmup epochs (backtest.start), data window starts at {}",
             tradingStartEpochs, ToString(maxStartTime, "%Y-%m-%d"));
    }

    // 使用对齐后的共同 bar 数量，而不是第一个 symbol 的 bar 数
    if (commonBars == std::numeric_limits<size_t>::max()) {
        commonBars = 0;  // 没有有效数据
    }
    context->setTotalBars(commonBars);
    context->reserveDailySnapshots(commonBars);

    // === 加载持仓事件（分红/送股等被动事件）===
    if (FinanceDB::instance().isInitialized()) {
        for (auto symbol : symbols) {
            std::string symStr = get_symbol(symbol);
            auto dbEvents = FinanceDB::instance().getDividendEvents(symStr);
            if (dbEvents.empty()) continue;

            Vector<std::unique_ptr<IPositionEvent>> events;
            for (auto& e : dbEvents) {
                events.push_back(std::make_unique<StockDividendEvent>(
                    e.ex_dividend_date, symbol,
                    e.bonus_per_10, e.transfer_per_10, e.cash_per_10
                ));
            }
            context->setPositionEvents(symbol, std::move(events));
            INFO("Loaded {} dividend events for {}", dbEvents.size(), symStr);
        }
    }

    _backtestContexts.emplace(runId, std::move(context));

    DEBUG_INFO("Created backtest context: runId={}, strategy={}, symbols={}, initialCapital={}",
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

bool HistorySimulationBase::stepForward(BacktestContext* context) {
    auto& symbols = context->getSymbols();
    if (symbols.empty()) {
        WARN("[stepForward] context has no symbols!");
        return false;
    }

    bool anyMoreData = false;
    bool anyFinished = false;
    static int _sfCallCount = 0;
    _sfCallCount++;
    if (_sfCallCount == 1) {
        // 首次调用：dump 第一个 symbol 的数据量
        auto firstSym = *symbols.begin();
        auto itr = _csvs.find(firstSym);
        size_t dataSize = (itr != _csvs.end()) ? itr->second.size() : 0;
        INFO("[stepForward] first call: symbols={}, _csvs.size={}, first_sym={} dataSize={}",
             symbols.size(), _csvs.size(), get_symbol(firstSym), dataSize);
    }

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

        const auto& data = itr->second;
        if (data.empty()) continue;

        uint32_t curIndex = context->getCurIndex(symbol);
        if (curIndex < data.size()) {
            if (data._datetime[curIndex] < minTime)
                minTime = data._datetime[curIndex];
        }
        else {
            context->setFinished(true);
            anyFinished = true;
            break;
        }

        auto org_itr = _org_csvs.find(symbol);
        QuoteInfo info;
        info._symbol = symbol;
        info._volume = data._volume[curIndex];
        info._time = data._datetime[curIndex];

        if (org_itr != _org_csvs.end() && !org_itr->second.empty()) {
            const auto& org = org_itr->second;
            uint32_t org_index = curIndex;
            if (org_index >= org.size()) {
                org_index = org.size() - 1;
            }
            info._open = org._open[org_index];
            info._close = org._close[org_index];
            info._high = org._high[org_index];
            info._low = org._low[org_index];
            info._adj_open  = data._open[curIndex];
            info._adj_close = data._close[curIndex];
            info._adj_high  = data._high[curIndex];
            info._adj_low   = data._low[curIndex];
        }
        else {
            info._open = data._open[curIndex];
            info._close = data._close[curIndex];
            info._high = data._high[curIndex];
            info._low = data._low[curIndex];
            info._adj_open  = data._open[curIndex];
            info._adj_close = data._close[curIndex];
            info._adj_high  = data._high[curIndex];
            info._adj_low   = data._low[curIndex];
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

    // === 持仓事件处理（分红/送股等被动事件）===
    {
        const QuoteInfo* firstQuote = nullptr;
        for (auto symbol : symbols) {
            const QuoteInfo* q = context->getQuote(symbol);
            if (q && q->_time > 0) { firstQuote = q; break; }
        }
        if (firstQuote) {
            time_t barDate = DateOnly(firstQuote->_time);
            context->processPositionEvents(barDate, firstQuote->_close);
        }
    }

    // === 计算调整系数 ===
    for (auto symbol : symbols) {
        auto hfq_itr = _csvs.find(symbol);
        auto org_itr = _org_csvs.find(symbol);

        if (hfq_itr == _csvs.end() || org_itr == _org_csvs.end() ||
            hfq_itr->second.empty() || org_itr->second.empty()) {
            context->setCurrentAdjRatio(symbol, 1.0);
            continue;
        }

        const auto& hfq = hfq_itr->second;
        const auto& org = org_itr->second;

        auto curIndex = context->getCurIndex(symbol);
        uint32_t priceIndex = (curIndex > 0) ? curIndex - 1 : 0;

        if (priceIndex < hfq.size() && priceIndex < org.size()) {
            double hfqClose = hfq._close[priceIndex];
            double origClose = org._close[priceIndex];
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
            if (position == 0) {
                assetValues[symbol] = 0.0;
                continue;
            }

            // 空头仓位（position < 0）必须计入负市值（融券负债）。
            // 早前用 position <= 0 直接跳过，配对交易 / 做空策略下空头腿负债
            // 被整条漏掉：卖出腿所得计入 cash 而对应负债不进权益，
            // 建仓当天权益凭空跳增（实测配对策略出现 +50% 的单日收益），
            // 且高估幅度 = 空头市值，会随仓位放大并正反馈。
            // 纯做多策略不受影响（position 恒 > 0）。
            //
            // 使用原始价格估值（与 cash 扣减同一价格体系，消除 ratio 漂移导致的虚假收益）
            auto org_itr = _org_csvs.find(symbol);
            if (org_itr == _org_csvs.end() || org_itr->second.empty()) continue;

            const auto& data = org_itr->second;
            auto curIndex = context->getCurIndex(symbol);
            uint32_t priceIndex = (curIndex > 0) ? curIndex - 1 : 0;
            if (priceIndex < data.size()) {
                double origClose = data._close[priceIndex];
                double assetValue = static_cast<double>(position) * origClose;
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

            // 记录快照后，用资产收益率更新 CUSUM 检测（不依赖策略是否交易）
            for (auto symbol : symbols) {
                auto itr = _csvs.find(symbol);
                if (itr == _csvs.end()) continue;
                const auto& data = itr->second;
                if (data.empty()) continue;
                auto curIndex = context->getCurIndex(symbol);
                uint32_t priceIndex = (curIndex > 0) ? curIndex - 1 : 0;
                if (priceIndex < 1) continue;

                double cur = data._close[priceIndex];
                double prev = data._close[priceIndex - 1];
                if (std::abs(prev) > 1e-10) {
                    context->updateCUSUM((cur - prev) / prev);
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
    if (org_itr == _org_csvs.end() || org_itr->second.empty()) {
        return GetAdjPrice(symbol, index);
    }
    const auto& org = org_itr->second;
    if (index >= org.size()) {
        index = org.size() - 1;
    }
    return org._close[index];
}

double HistorySimulationBase::GetAdjPrice(symbol_t symbol, uint32_t index) const {
    auto itr = _csvs.find(symbol);
    if (itr == _csvs.end() || itr->second.empty()) {
        return 0.0;
    }
    const auto& data = itr->second;
    if (index >= data.size()) {
        index = data.size() - 1;
    }
    return data._close[index];
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
        String cmd = std::string(PYTHON_CMD) + " " + _org_path + "/../tools/run_task.py 1";
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
        String cmd = std::string(PYTHON_CMD) + " " + _org_path + "/../tools/run_task.py 2";
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
                String filename = entry.path().stem().string();
                info._code = filename;
                info._name = info._code;  // 暂无名称，使用代码

                // 解析 CODE.EXCHANGE 格式 (如 510300.SH)
                auto dot_pos = filename.find('.');
                if (dot_pos != String::npos && dot_pos > 0) {
                    String code_part = filename.substr(0, dot_pos);
                    String exchange_part = filename.substr(dot_pos + 1);
                    
                    // 转换为大写进行比较
                    std::transform(exchange_part.begin(), exchange_part.end(), 
                                   exchange_part.begin(), ::toupper);
                    
                    if (exchange_part == "SH" || exchange_part == "SSE") {
                        info._exchange = MT_Shanghai;
                        info._code = code_part;  // 只保留代码部分
                    } else if (exchange_part == "SZ" || exchange_part == "SZSE") {
                        info._exchange = MT_Shenzhen;
                        info._code = code_part;  // 只保留代码部分
                    } else {
                        continue;  // 无法识别的交易所格式
                    }
                } else {
                    // 兼容旧格式：纯6位数字代码，根据首位判断
                    if (filename.size() < 6) continue;
                    char firstDigit = filename[0];
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
        String cmd = std::string(PYTHON_CMD) + " " + _org_path + "/../tools/run_task.py 3";
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
    _reports.clear();
    _cur_id = 0;

    // 不销毁回测上下文，由外部管理
}

void HistorySimulationBase::SetBacktestTimeRange(time_t start, time_t end) {
    _hasBacktestTimeRange = true;
    _backtestStartTime = start;
    _backtestEndTime = end;
}

void HistorySimulationBase::ClearBacktestTimeRange() {
    _hasBacktestTimeRange = false;
    _backtestStartTime = 0;
    _backtestEndTime = 0;
}

void HistorySimulationBase::SetLoadDateRange(const String& startDate, const String& endDate) {
    _loadStartDate = startDate;
    _loadEndDate = endDate;
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
