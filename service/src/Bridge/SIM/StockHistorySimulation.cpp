#include "Bridge/SIM/StockHistorySimulation.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "boost/lockfree/queue.hpp"
#include "std_header.h"
#include "csv.h"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory_resource>
#include <numeric>
#include <thread>
#include <utility>
#include "yas/detail/type_traits/flags.hpp"
#include "server.h"
#include "BrokerSubSystem.h"

StockHistorySimulation::StockHistorySimulation(Server* server)
  :ExchangeInterface(server),_cur_index(0), _worker(nullptr), _cur_id(0)
  ,_finish(false)
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
  if (_worker) {
    _worker->join();
    delete _worker;
    _worker = nullptr;
  }
  _orders.visit_all([](auto&& item) {
      delete item.second;
      });
  return true;
}

bool StockHistorySimulation::Login(AccountType t){
    _finish = false;
    // 加载配置中的买入/卖出手续费
    auto& config = _server->GetConfig();
    
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

order_id StockHistorySimulation::AddOrder(const symbol_t& symbol, OrderContext* order){
    // 买入时检查并冻结资金
    if (order->_order._side == 0) {  // 买入
        double orderCost = order->_order._price * order->_order._volume;

        double current = _availableFunds.load(std::memory_order_relaxed);
        double expected = current;
        // 使用 CAS 保证原子性：检查和扣减一起完成
        while (true) {
            if (expected < orderCost) {
                // 资金不足，拒绝下单
                WARN("资金不足：所需 {:.2f}，可用 {:.2f}", orderCost, expected);
                order_id id;
                id._id = 0;  // 返回0表示拒绝
                return id;
            }
            if (_availableFunds.compare_exchange_strong(expected, expected - orderCost,
                    std::memory_order_release, std::memory_order_relaxed)) {
                break;
            }
            // expected 已被更新为当前值，继续循环重试
        }
    }

    OrderInfo info;
    info._id = ++_cur_id;
    info._order = order;
    _reports.emplace(info._id, order);
    // _orders.try_emplace_or_visit(symbol, info, [](auto&){
      
    // });
    if (_orders.count(symbol) == 0) {
        std::pair<symbol_t, boost::lockfree::queue<OrderInfo>*> pr;
        pr.first = symbol;
        pr.second = new boost::lockfree::queue<OrderInfo>(MAX_ORDER_PER_SECOND);
        pr.second->push(info);
       _orders.emplace(std::move(pr));
    }
    else {
        _orders.visit(symbol, [&info](auto&& item) {
            item.second->push(info);
            });
    }
    order_id id;
    id._id = info._id;
    if (is_stock(symbol)) {
      id._type = 0;
    }
    return id;
}

void StockHistorySimulation::OnOrderReport(order_id id, const TradeReport& report) {
    auto broker = _server->GetBrokerSubSystem();
    _reports.visit(id._id, [&report, broker, this](auto&& value) {
        auto ctx = value.second;
        value.second->_trades._reports.emplace_back(report);
        // 交易记录
        broker->RecordTrade(*ctx);

        // 更新持仓跟踪
        {
            std::lock_guard<std::mutex> lock(_positionMtx);
            auto& pos = _positions[ctx->_order._symbol];
            if (ctx->_order._side == 0) {  // 买入
                pos += report._quantity;
            } else {  // 卖出
                pos -= report._quantity;
            }
        }

        // 回调通知完成
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

    // 立即触发数据加载，而不是等到 QueryQuotes
    if (!std::filesystem::exists(_org_path)) {
        WARN("{} not exist.", _org_path);
        return;
    }

    // 预加载 symbol 信息到缓存
    //UseLevel(1);  // 加载日线数据
    //_worker = new std::thread(&StockHistorySimulation::Worker, this);
}

void StockHistorySimulation::UseLevel(int level) {
    if (level == 1) {
        for (auto& code : _filter._symbols) {
            LoadT1(code);
        }
    }
    else {
        //
        for (auto& code : _filter._symbols) {
            LoadT0(code);
        }
    }
    // 记录总数据量（以第一个标的为准，用于进度计算）
    if (!_csvs.empty()) {
        _totalSize = _csvs.begin()->second.get_index().size();
        INFO("Set total data size: {}", _totalSize);
    }
}

#define CACHE_SIZE  2048

// 辅助函数：加载 CSV 文件到 DataFrame
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
            // 加载 6 列 header: date, open, close, high, low, volume
            for (int i = 0; i < 6; ++i) {
                header.emplace_back(row[i]);
            }
            continue;
        }
        dates.emplace_back(FromStr(row[0], "%Y-%m-%d %H:%M:%S"));
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

    // 加载复权数据
    if (!LoadCSVToDataFrame(file_path, _csvs[symbol], _headers[symbol])) {
        String err_msg = fmt::format("Failed to load backtest data for '{}': CSV file not found or invalid at '{}'. "
                                     "Please ensure the code format is correct (e.g., 'sh.600519' or 'sz.000001') "
                                     "and the CSV file exists in the data directory.", code, file_path);
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }

    // 加载原始价格数据（AStock）
    if (!LoadCSVToDataFrame(primitive_file_path, _org_csvs[symbol], _org_headers[symbol])) {
        WARN("load {} fail, will use adjusted price", primitive_file_path);
        // 如果原始数据加载失败，复制复权数据作为原始数据
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
  // 5s一次，请求一组信息
  _cv.notify_all();
}

bool StockHistorySimulation::Once(uint32_t& curIndex) {
    if (_csvs.empty()) {
        WARN("Quote is empty");
        _finish = true;
        _dataLoadSuccess = false;  // 标记失败
        _finishCv.notify_all();    // 通知等待者
        return false;
    }
    for (auto& df : _csvs) {
        auto num = df.second.get_index().size();
        if (curIndex >= num - 1) {
            _finish = true;
            _dataLoadSuccess = true;  // 标记正常完成
            INFO("_finish true, data loaded successfully");
            curIndex = 0;
            _finishCv.notify_all();   // 通知等待者
            return false;
        }

        auto& header = _headers[df.first];

        auto& datetime = df.second.get_column<time_t>(header[0].c_str());
        auto& open = df.second.get_column<float>(header[1].c_str());
        auto& close = df.second.get_column<float>(header[2].c_str());
        auto& high = df.second.get_column<float>(header[3].c_str());
        auto& low = df.second.get_column<float>(header[4].c_str());
        auto& volume = df.second.get_column<int64_t>(header[5].c_str());

        QuoteInfo info;
        info._symbol = df.first;
        info._open = open[curIndex];
        info._close = close[curIndex];  // 复权价，用于指标计算
        info._high = high[curIndex];
        info._low = low[curIndex];
        info._volume = volume[curIndex];
        info._time = datetime[curIndex];

        // 发送复权价给 QuoteInputNode（用于指标计算）
        yas::shared_buffer buf = yas::save<flags>(info);
        if (0 != nng_send(_sock, buf.data.get(), buf.size, NNG_FLAG_NONBLOCK)) {
            printf("send quote message e fail.\n");
            return false;
        }

        // 获取原始价格用于订单撮合
        double primitiveClose = GetPrimitivePrice(df.first, curIndex);
        auto symbol = df.first;
        _orders.visit(df.first, [&info, &symbol, primitiveClose, this] (auto&& que) {
            OrderInfo oif;
            while (que.second->pop(oif)) {
                // 使用原始价格进行撮合
                QuoteInfo matchQuote = info;
                matchQuote._close = primitiveClose;
                TradeReport report = OrderMatch(oif._order->_order, matchQuote);

                // 卖出成交：释放资金（买入资金在AddOrder已冻结）
                if (oif._order->_order._side == 1) {  // 卖出
                    _availableFunds.fetch_add(report._trade_amount, std::memory_order_release);
                }

                oif._order->_trades._symbol = symbol;
                order_id id;
                id._id = static_cast<uint32_t>(oif._id);
                OnOrderReport(id, report);
            }
        });
        _quotes[df.first] = std::move(info);
    }
    ++curIndex;
    return true;
}

bool StockHistorySimulation::Once(symbol_t symbol, std::atomic<uint32_t>& curIndex) {
    constexpr std::size_t flags = yas::mem | yas::binary;

    auto itr = _csvs.find(symbol);
    if (itr == _csvs.end()) {
        WARN("Symbol {} not found in loaded data", symbol);
        return false;
    }

    auto& df = itr->second;
    auto num = df.get_index().size();
    if (curIndex >= num - 1) {
        _finish = true;
        _dataLoadSuccess = true;  // 标记正常完成
        INFO("_finish true for symbol {}", symbol);
        curIndex = 0;
        _finishCv.notify_all();   // 通知等待者
        return false;
    }

    auto& header = _headers[symbol];
    auto& datetime = df.get_column<time_t>(header[0].c_str());
    auto& open = df.get_column<float>(header[1].c_str());
    auto& close = df.get_column<float>(header[2].c_str());
    auto& high = df.get_column<float>(header[3].c_str());
    auto& low = df.get_column<float>(header[4].c_str());
    auto& volume = df.get_column<int64_t>(header[5].c_str());

    QuoteInfo info;
    info._symbol = symbol;
    info._open = open[curIndex];
    info._close = close[curIndex];  // 复权价，用于指标计算
    info._high = high[curIndex];
    info._low = low[curIndex];
    info._volume = volume[curIndex];
    info._time = datetime[curIndex];

    // 获取原始价格用于订单撮合
    double primitiveClose = GetPrimitivePrice(symbol, curIndex);

    // 处理该 symbol 的挂单
    if (_orders.count(symbol) > 0) {
        _orders.visit(symbol, [&info, &symbol, primitiveClose, this](auto&& que) {
            OrderInfo oif;
            while (que.second->pop(oif)) {
                // 使用原始价格进行撮合
                QuoteInfo matchQuote = info;
                matchQuote._close = primitiveClose;
                TradeReport report = OrderMatch(oif._order->_order, matchQuote);

                // 卖出成交：释放资金（买入资金在 AddOrder 已冻结）
                if (oif._order->_order._side == 1) {  // 卖出
                    _availableFunds.fetch_add(report._trade_amount, std::memory_order_release);
                }

                oif._order->_trades._symbol = symbol;
                order_id id;
                id._id = static_cast<uint32_t>(oif._id);
                OnOrderReport(id, report);
            }
        });
    }

    _quotes[symbol] = std::move(info);
    ++curIndex;
    return true;
}

double StockHistorySimulation::GetAvailableFunds()
{
    return _availableFunds.load(std::memory_order_relaxed);
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

void StockHistorySimulation::Worker() {
  Publish(URI_RAW_QUOTE, _sock);
  constexpr std::size_t flags = yas::mem | yas::binary;
  _finish = true;
  thread_local uint32_t curIndex = 0;
  while (!_server->IsExit()) {
    // notifys
    std::unique_lock<std::mutex> lock(_mx);
    auto status = _cv.wait_for(lock, std::chrono::seconds(5));
    if (status == std::cv_status::timeout) {
      continue;
    }
    if (_csvs.size() == 0) {
        continue;
    }
    Once(curIndex);
  }
  _finish = true;
  nng_close(_sock);
}

void StockHistorySimulation::SetCommission(const Commission& buy, const Commission& sell) {
  _buy = buy;
  _sell = sell;
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
    auto fut = std::async(std::launch::deferred, [this, symbol]() {
        Once(symbol, _cur_index);
    });
    fut.wait();
    auto info = _quotes[symbol];
    if (_finish) {
        info._time = 0;
    }
    return info;
}

double StockHistorySimulation::Progress() {
    // 如果没有数据，返回 0
    if (_csvs.empty()) {
        return 0.0;
    }

    // 优先使用预设的总大小（UseLevel 中设置）
    if (_totalSize > 0) {
        double progress = 1.0 * _cur_index.load() / _totalSize;
        return std::min(1.0, std::max(0.0, progress));
    }

    // 兜底：使用第一个 symbol 的数据大小
    auto itr = _csvs.begin();
    auto size = itr->second.get_index().size();

    // 避免除零
    if (size == 0) {
        return 0.0;
    }

    // 计算进度：当前索引 / 总大小
    double progress = 1.0 * _cur_index.load() / size;

    // 限制在 [0, 1] 范围内
    return std::min(1.0, std::max(0.0, progress));
}

bool StockHistorySimulation::WaitForBacktestComplete(int timeoutSeconds) {
    std::unique_lock<std::mutex> lock(_finishMtx);

    // 等待完成信号或超时
    bool completed = _finishCv.wait_for(lock,
        std::chrono::seconds(timeoutSeconds),
        [this]() {
            return _finish.load() || _server->IsExit();
        });

    if (!completed) {
        WARN("Backtest wait timeout after {} seconds", timeoutSeconds);
        return false;
    }

    // 检查是否正常完成
    if (!_dataLoadSuccess.load()) {
        WARN("Backtest finished but data load failed");
        return false;
    }

    INFO("Backtest completed successfully");
    return true;
}

bool StockHistorySimulation::IsBacktestComplete() const {
    return _finish.load() && _dataLoadSuccess.load();
}

// ============ 合约信息查询接口实现 ============

bool StockHistorySimulation::GetAllStockSymbols(List<SymbolInfo>& symbols) {
    String csv_path = _org_path + "/symbol_market.csv";

    // 如果 CSV 文件不存在，调用脚本生成
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
            info._code = code.substr(2); // 去掉"sh"/"sz"前缀
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

            // 判断看涨/看跌期权
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

    // 先尝试从股票中查找
    List<SymbolInfo> stocks;
    if (GetAllStockSymbols(stocks)) {
        for (const auto& s : stocks) {
            if (s._code == code) {
                return s;
            }
        }
    }

    // 再尝试从基金中查找
    List<SymbolInfo> funds;
    if (GetAllFundSymbols(funds)) {
        for (const auto& s : funds) {
            if (s._code == code) {
                return s;
            }
        }
    }

    // 最后尝试从期权中查找
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

double StockHistorySimulation::GetPrimitivePrice(symbol_t symbol, uint32_t index) {
    auto org_itr = _org_csvs.find(symbol);
    if (org_itr == _org_csvs.end()) {
        // 没有原始数据，返回复权价
        return GetAdjPrice(symbol, index);
    }
    auto& org_df = org_itr->second;
    auto& org_header = _org_headers[symbol];
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
    std::lock_guard<std::mutex> lock(_positionMtx);
    auto itr = _positions.find(symbol);
    if (itr == _positions.end()) {
        return 0;
    }
    return static_cast<int64_t>(itr->second);
}

double StockHistorySimulation::GetAdjPrice(symbol_t symbol, uint32_t index) {
    auto itr = _csvs.find(symbol);
    if (itr == _csvs.end()) {
        return 0.0;
    }
    auto& df = itr->second;
    auto& header = _headers[symbol];
    if (header.empty()) {
        return 0.0;
    }
    auto& close = df.get_column<float>(header[2].c_str());
    if (index >= close.size()) {
        index = close.size() - 1;
    }
    return close[index];
}
