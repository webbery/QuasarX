#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/BacktestContext.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "std_header.h"
#include "csv.h"
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

StockHistorySimulation::StockHistorySimulation(Server* server)
  : ExchangeInterface(server), _cur_id(0), _finish(false)
{

}

StockHistorySimulation::~StockHistorySimulation() {

}

bool StockHistorySimulation::Init(const ExchangeInfo& handle) {
    _org_path = handle._quote_addr;
    String dbpath = handle._local_addr;
    return true;
}

bool StockHistorySimulation::Release() {
  Clear();
  return true;
}

bool StockHistorySimulation::Login(AccountType t){
    _finish = false;
    Clear();

    if (_freqType == 1) {
        for (auto& code : _filter._symbols) {
            LoadT1(code);
        }
    }
    else {
        for (auto& code : _filter._symbols) {
            LoadT0(code);
        }
    }
    return true;
}

void StockHistorySimulation::Logout(AccountType t) {
    _finish = true;
}

bool StockHistorySimulation::IsLogin() {
  return !_finish;
}

bool StockHistorySimulation::GetSymbolExchanges(List<Pair<String, ExchangeName>>& info)
{
    return true;
}

bool StockHistorySimulation::GetPosition(AccountPosition& pos){
    return true;
}

AccountAsset StockHistorySimulation::GetAsset(){
    AccountAsset ass;
    return ass;
}

order_id StockHistorySimulation::AddOrder(uint16_t run_id, const symbol_t& symbol, OrderContext* order){
    // run_id: 回测运行 ID，用于区分不同的策略实例
    // 买入时检查并冻结资金
    if (order->_order._side == 0) {  // 买入
        double orderCost = order->_order._price * order->_order._volume;

        double current = 0;
        _backtestContexts.visit(run_id, [&current](auto& item) {
            current = item.second->getAvailableFunds();
            });
        double expected = current;
        while (true) {
            if (expected < orderCost) {
                WARN("资金不足：所需 {:.2f}，可用 {:.2f}", orderCost, expected);
                order_id id;
                id._id = 0;
                return id;
            }
            current -= orderCost;
            _backtestContexts.visit(run_id, [&current](auto& item) {
                item.second->setAvailableFunds(current);
                });
            break;
        }
    }

    OrderInfo info;
    info._id = ++_cur_id;
    info._order = order;
    _reports.emplace(info._id, order);

    order_id id;
    id._id = info._id;
    if (is_stock(symbol)) {
      id._type = 0;
    }
    return id;
}

void StockHistorySimulation::OnOrderReport(order_id id, const TradeReport& report) {
    // 在上下文中查找订单（多线程模式）
    bool found = false;
    _backtestContexts.visit_all([&found, &id, &report, this](auto& item) {
        if (found) return;

        auto* ctx = item.second.get();
        auto* orderCtx = ctx->getOrderReport(id._id);
        if (orderCtx) {
            found = true;
            orderCtx->Update(report);
            orderCtx->_trades._reports.emplace_back(report);
            orderCtx->_success.store(true);
            orderCtx->_flag.store(true);
            orderCtx->_promise.set_value(true);

            // 更新持仓（使用上下文私有持仓）
            if (orderCtx->_order._side == 0) {  // 买入
                ctx->adjustPosition(orderCtx->_order._symbol, report._quantity);
            } else {  // 卖出
                ctx->adjustPosition(orderCtx->_order._symbol, -report._quantity);
            }

            // 记录交易
            auto broker = _server->GetBrokerSubSystem();
            if (broker) {
                broker->RecordTrade(*orderCtx);
            }
        }
    });

    if (found) return;

    // 全局回退：使用 _reports
    auto broker = _server->GetBrokerSubSystem();
    _reports.visit(id._id, [&report, broker](auto&& value) {
        auto ctx = value.second;
        value.second->_trades._reports.emplace_back(report);
        broker->RecordTrade(*ctx);

        ctx->Update(report);
        value.second->_success.store(true);
        value.second->_flag.store(true);
        value.second->_promise.set_value(true);
    });
}

Boolean StockHistorySimulation::CancelOrder(order_id id, OrderContext* order){
    return true;
}

bool StockHistorySimulation::GetOrders(SecurityType type, OrderList& ol)
{
    return true;
}

bool StockHistorySimulation::GetOrder(const String& sysID, Order& ol)
{
    INFO("StockSimulation GetOrder");
    return true;
}

void StockHistorySimulation::SetFilter(const QuoteFilter& filter) {
    _filter = filter;

    if (!std::filesystem::exists(_org_path)) {
        WARN("{} not exist.", _org_path);
        return;
    }
}

void StockHistorySimulation::UseLevel(int level) {
    _freqType = level;
}

#define CACHE_SIZE  2048

bool StockHistorySimulation::LoadCSVToDataFrame(const String& file_path,
                                                 DataFrame& df,
                                                 Vector<String>& header) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return false;
    }

    INFO("load {} success", file_path);
    char cache[CACHE_SIZE] = { 0 };
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
        // 智能判断日期格式：如果长度<=10则只有日期，否则包含时间
        const char* timeFmt = (row[0].size() <= 10) ? "%Y-%m-%d" : "%Y-%m-%d %H:%M:%S";
        if (row[0] == "2022-03-11") {
            INFO("{} {} {}", file_path, timeFmt, FromStr(row[0], timeFmt));
        }
        dates.emplace_back(FromStr(row[0], timeFmt));
        open.emplace_back(std::stof(row[1]));
        close.emplace_back(std::stof(row[2]));
        high.emplace_back(std::stof(row[3]));
        low.emplace_back(std::stof(row[4]));
        volume.emplace_back(std::stol(row[5]));
    }
    ifs.close();

    if (header.empty()) {
        return false;
    }

    Vector<uint32_t> indexes(index);
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

void StockHistorySimulation::LoadT1(const String& code) {
    auto& security = Server::GetSecurity(code);
    auto symbol = to_symbol(code, security);
    String subdir, orgdir;
    if (is_stock(symbol)) {
        subdir = "A_hfq";
        orgdir = "AStock";
    }
    auto file_path = _org_path + "/" + subdir + "/" + code + ".csv";
    auto primitive_file_path = _org_path + "/" + orgdir + "/" + code + ".csv";

    if (!LoadCSVToDataFrame(file_path, _csvs[symbol], _headers[symbol])) {
        String err_msg = fmt::format("Failed to load backtest data for '{}': CSV file not found or invalid at '{}'. "
                                     "Please ensure the code format is correct (e.g., 'sh.600519' or 'sz.000001') "
                                     "and the CSV file exists in the data directory.", code, file_path);
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }

    if (!LoadCSVToDataFrame(primitive_file_path, _org_csvs[symbol], _org_headers[symbol])) {
        WARN("load {} fail, will use adjusted price", primitive_file_path);
        _org_csvs[symbol] = _csvs[symbol];
        _org_headers[symbol] = _headers[symbol];
    }
}

void StockHistorySimulation::LoadT0(const String& code) {
  auto& security = Server::GetSecurity(code);
  auto symbol = to_symbol(code, security);
  String subdir;
  if (is_stock(symbol)) {
    subdir = "stock";
  }
  auto file_path = _org_path + "/zh/" + subdir + "/" + code + ".csv";

  if (!LoadCSVToDataFrame(file_path, _csvs[symbol], _headers[symbol])) {
      String err_msg = fmt::format("Failed to load backtest data for '{}': CSV file not found or invalid at '{}'. "
                                   "Please ensure the code format is correct (e.g., 'sh.600519' or 'sz.000001') "
                                   "and the CSV file exists in the data directory.", code, file_path);
      WARN("{}", err_msg);
      throw std::runtime_error(err_msg);
  }
}

void StockHistorySimulation::QueryQuotes() {
  // 多线程回测模式下不需要主动查询，由 stepForward 推进时间
}

double StockHistorySimulation::GetAvailableFunds(uint16_t run_id)
{
    double funds = BACKTEST_INITIAL_CAPITAL;
    _backtestContexts.visit(run_id, [&funds](auto& item) {
        funds = item.second->getAvailableFunds();
        });
    return funds;
}

bool StockHistorySimulation::GetCommission(symbol_t symbol, List<Commission>& comms) {
  return true;
}

Boolean StockHistorySimulation::HasPermission(symbol_t symbol)
{
    return true;
}

void StockHistorySimulation::Reset()
{

}

void StockHistorySimulation::SetCommission(const Commission& buy, const Commission& sell) {
  _buy = buy;
  _sell = sell;
}

void StockHistorySimulation::Clear() {
    // 清空行情数据（只读共享数据）
    _csvs.clear();
    _org_csvs.clear();
    _headers.clear();
    _org_headers.clear();

    // 清空所有回测上下文（多线程模式）
    _backtestContexts.clear();
    _nextRunId = 1;

    // 清空订单和报告
    _reports.clear();
    _cur_id = 0;

}

int StockHistorySimulation::GetStockLimitation(char type)
{
    return 0;
}

bool StockHistorySimulation::SetStockLimitation(char type, int limitation)
{
    return false;
}

TradeReport StockHistorySimulation::OrderMatch(const Order& order, const QuoteInfo& quote)
{
    TradeReport report;
    report._price = order._price;
    report._time = quote._time;
    report._quantity = order._volume;
    report._side = order._side;
    report._trade_amount = order._volume * order._price;
    return report;
}

QuoteInfo StockHistorySimulation::GetQuote(symbol_t symbol) {
    QuoteInfo empty;
    empty._symbol = symbol;
    empty._time = 0;
    return empty;
}

double StockHistorySimulation::Progress(const String& strategy) {
    // 获取指定策略的回测上下文进度
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

// ============ 合约信息查询接口实现 ============

bool StockHistorySimulation::GetAllStockSymbols(List<SymbolInfo>& symbols) {
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

bool StockHistorySimulation::GetAllFundSymbols(List<SymbolInfo>& symbols) {
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

bool StockHistorySimulation::GetAllOptionSymbols(List<SymbolInfo>& symbols) {
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

SymbolInfo StockHistorySimulation::GetSymbolInfo(const String& code) {
    SymbolInfo info;

    List<SymbolInfo> stocks;
    if (GetAllStockSymbols(stocks)) {
        for (const auto& s : stocks) {
            if (s._code == code) {
                return s;
            }
        }
    }

    List<SymbolInfo> funds;
    if (GetAllFundSymbols(funds)) {
        for (const auto& s : funds) {
            if (s._code == code) {
                return s;
            }
        }
    }

    List<SymbolInfo> options;
    if (GetAllOptionSymbols(options)) {
        for (const auto& s : options) {
            if (s._code == code) {
                return s;
            }
        }
    }

    return info;
}

void StockHistorySimulation::RefreshSymbolList() {
    // 仿真环境不需要主动刷新，数据来自本地 CSV
}

double StockHistorySimulation::GetPrimitivePrice(symbol_t symbol, uint32_t index) const {
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

int64_t StockHistorySimulation::GetPositionQuantity(symbol_t symbol) const {
    int64_t position = 0;
    _backtestContexts.visit_all([&position, &symbol](const auto& item) {
        position = item.second->getPosition(symbol);
    });
    return position;
}

double StockHistorySimulation::GetAdjPrice(symbol_t symbol, uint32_t index) const {
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

// ============ 多线程回测支持实现 ============

/**
 * @brief 创建回测上下文，并计算所有标的的共同时间范围
 * 
 * 核心逻辑：
 * 1. 遍历所有 symbol，获取每个 symbol CSV 的起始和结束时间
 * 2. 计算共同时间范围：
 *    - commonStartTime = 所有标的起始时间的最大值
 *    - commonEndTime = 所有标的结束时间的最小值
 * 3. 将每个 symbol 的 curIndex 设置到 commonStartTime 对应的位置
 * 
 * 这样确保多标的回测时，所有标的数据在时间上是对齐的。
 */
uint16_t StockHistorySimulation::createBacktestContext(
    const String& strategy_name,
    const Set<symbol_t>& symbols,
    double initial_capital)
{
    uint16_t runId = _nextRunId.fetch_add(1, std::memory_order_relaxed);

    auto context = std::make_unique<BacktestContext>(runId, strategy_name);
    context->setCapital(initial_capital);

    // 第一步：计算所有标的的共同时间范围
    time_t maxStartTime = 0;  // 所有标的起始时间的最大值（确保所有标的都有数据）
    time_t minEndTime = std::numeric_limits<time_t>::max();  // 所有标的结束时间的最小值（确保不超出任何标的范围）

    for (auto symbol : symbols) {
        context->addSymbol(symbol);

        // 获取该标的 CSV 数据
        auto itr = _csvs.find(symbol);
        if (itr != _csvs.end()) {
            const auto& df = itr->second;
            const auto& header = _headers.at(symbol);

            if (df.get_index().size() > 0) {
                const auto& datetime = df.get_column<time_t>(header[0].c_str());

                // 获取该标的起始和结束时间
                time_t startTime = datetime[0];
                time_t endTime = datetime[df.get_index().size() - 1];

                // 更新共同时间范围
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

    // 设置共同时间范围到 context
    context->setCommonStartTime(maxStartTime);
    context->setCommonEndTime(minEndTime);

    INFO("Common time range: start={}, end={}",
         ToString(maxStartTime, "%Y-%m-%d %H:%M:%S"),
         ToString(minEndTime, "%Y-%m-%d %H:%M:%S"));

    // 第二步：将每个 symbol 的索引设置到对应共同起始时间的位置
    // 这样确保回测开始时，所有标的数据在时间上对齐
    for (auto symbol : symbols) {
        auto itr = _csvs.find(symbol);
        if (itr != _csvs.end()) {
            const auto& df = itr->second;
            const auto& header = _headers.at(symbol);
            const auto& datetime = df.get_column<time_t>(header[0].c_str());

            // 找到第一个时间 >= maxStartTime 的索引
            uint32_t startIndex = 0;
            for (uint32_t i = 0; i < datetime.size(); ++i) {
                if (datetime[i] >= maxStartTime) {
                    startIndex = i;
                    break;
                }
            }

            context->setCurIndex(symbol, startIndex);
            INFO("Symbol {} start index: {}", get_symbol(symbol), startIndex);
        } else {
            context->setCurIndex(symbol, 0);
        }
    }

    if (!_csvs.empty()) {
        context->setTotalBars(_csvs.begin()->second.get_index().size());
    }

    _backtestContexts.emplace(runId, std::move(context));

    INFO("Created backtest context: runId={}, strategy={}, symbols={}, initialCapital={}",
         runId, strategy_name, symbols.size(), initial_capital);

    return runId;
}

BacktestContext* StockHistorySimulation::getBacktestContext(uint16_t run_id) {
    BacktestContext* ctx = nullptr;
    _backtestContexts.visit(run_id, [&ctx](auto& item) {
        ctx = item.second.get();
    });
    return ctx;
}

const BacktestContext* StockHistorySimulation::getBacktestContext(uint16_t run_id) const {
    const BacktestContext* ctx = nullptr;
    _backtestContexts.visit(run_id, [&ctx](auto& item) {
        ctx = item.second.get();
    });
    return ctx;
}

void StockHistorySimulation::destroyBacktestContext(uint16_t run_id) {
    _backtestContexts.erase(run_id);
}

/**
 * @brief 推进回测时间，为每个 symbol 加载下一个 bar 的数据
 * 
 * 逻辑：
 * 1. 检查是否已超过共同结束时间
 * 2. 遍历所有 symbol，读取当前索引对应的数据
 * 3. 将数据写入 context 的 quote 缓存
 * 4. 推进每个 symbol 的索引
 * 
 * @return true 如果还有更多数据，false 如果回测应该结束
 */
bool StockHistorySimulation::stepForward(BacktestContext* context) {
    if (!context || context->isFinished()) {
        return false;
    }

    // 检查是否所有标的都已到达或超过共同结束时间
    time_t commonEndTime = context->getCommonEndTime();
    if (commonEndTime > 0) {
        bool allAtEnd = true;
        const auto& symbols = context->getSymbols();

        for (auto symbol : symbols) {
            auto itr = _csvs.find(symbol);
            if (itr == _csvs.end()) {
                continue;
            }

            const auto& df = itr->second;
            const auto& header = _headers.at(symbol);
            auto curIndex = context->getCurIndex(symbol);

            // 如果该标的还没到末尾，说明还有数据
            if (curIndex < df.get_index().size()) {
                const auto& datetime = df.get_column<time_t>(header[0].c_str());
                if (curIndex < datetime.size() && datetime[curIndex] <= commonEndTime) {
                    allAtEnd = false;
                    break;
                }
            }
        }

        // 所有标的都已到达结束时间，终止回测
        if (allAtEnd) {
            INFO("All symbols reached common end time, backtest completed");
            context->setFinished(true);
            return false;
        }
    }

    bool anyMoreData = false;
    const auto& symbols = context->getSymbols();

    std::shared_lock<std::shared_mutex> dataLock(_dataMutex);

    for (auto symbol : symbols) {
        auto itr = _csvs.find(symbol);
        if (itr == _csvs.end()) {
            continue;
        }

        const auto& df = itr->second;
        const auto& header = _headers.at(symbol);
        auto curIndex = context->getCurIndex(symbol);

        if (curIndex >= df.get_index().size() - 1) {
            continue;
        }

        const auto& datetime = df.get_column<time_t>(header[0].c_str());
        const auto& open = df.get_column<float>(header[1].c_str());
        const auto& close = df.get_column<float>(header[2].c_str());
        const auto& high = df.get_column<float>(header[3].c_str());
        const auto& low = df.get_column<float>(header[4].c_str());
        const auto& volume = df.get_column<int64_t>(header[5].c_str());

        QuoteInfo info;
        info._symbol = symbol;
        info._open = open[curIndex];
        info._close = close[curIndex];
        info._high = high[curIndex];
        info._low = low[curIndex];
        info._volume = volume[curIndex];
        info._time = datetime[curIndex];

        context->setQuote(symbol, info);
        context->incrementCurIndex(symbol);
        anyMoreData = true;
    }

    dataLock.unlock();

    for (auto symbol : symbols) {
        matchOrders(context, symbol);
    }

    if (!anyMoreData) {
        context->setFinished(true);
    }

    return anyMoreData;
}

void StockHistorySimulation::matchOrders(BacktestContext* context, symbol_t symbol) {
    auto* queue = context->getOrderQueue(symbol);
    if (!queue) return;

    const QuoteInfo* quote = context->getQuote(symbol);
    if (!quote) return;

    OrderInfo orderInfo;
    while (queue->pop(orderInfo)) {
        uint32_t curIndex = context->getCurIndex(symbol);
        if (curIndex > 0) curIndex--;
        double primitivePrice = GetPrimitivePrice(symbol, curIndex);

        QuoteInfo matchQuote = *quote;
        matchQuote._close = primitivePrice;

        TradeReport report = OrderMatch(orderInfo._order->_order, matchQuote);

        if (orderInfo._order->_order._side == 1) {
            context->releaseFunds(report._trade_amount);
        }

        // 先注册订单报告，以便 OnOrderReport 能查找到
        context->addOrderReport(orderInfo._id, orderInfo._order);

        // 调用 OnOrderReport 统一处理订单成交报告
        // OnOrderReport 会处理：持仓调整、交易记录、订单状态更新
        order_id id;
        id._id = orderInfo._id;
        OnOrderReport(id, report);
    }
}

QuoteInfo StockHistorySimulation::GetQuote(symbol_t symbol, run_id_t run_id) {
    // 查找对应策略的回测上下文
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

order_id StockHistorySimulation::AddOrder(const symbol_t& symbol, OrderContext* order, uint32_t strategy_hash) {
    // 查找对应策略的回测上下文
    BacktestContext* ctx = nullptr;
    _backtestContexts.visit_all([&ctx, strategy_hash](auto& item) {
        uint32_t hs = std::hash<String>{}(item.second->getStrategyName());
        if (hs == strategy_hash) {
            ctx = item.second.get();
        }
    });

    if (!ctx) {
        WARN("Backtest context not found for strategy: {}", strategy_hash);
        order_id id;
        id._id = 0;
        return id;
    }

    // 买入时检查并冻结资金
    if (order->_order._side == 0) {
        double orderCost = order->_order._price * order->_order._volume;
        if (!ctx->tryReserveFunds(orderCost)) {
            WARN("资金不足：所需 {:.2f}，可用 {:.2f}", orderCost, ctx->getAvailableFunds());
            order_id id;
            id._id = 0;
            return id;
        }
    }

    OrderInfo info;
    info._id = ++_cur_id;
    info._order = order;

    auto* queue = ctx->getOrCreateOrderQueue(symbol);
    queue->push(info);

    order_id id;
    id._id = static_cast<uint32_t>(info._id);
    if (is_stock(symbol)) {
        id._type = 0;
    }
    return id;
}
