#include "server.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include "Bridge/exchange.h"
#include "BrokerSubSystem.h"
#include "DataFrame/DataFrameTypes.h"
#include "Handler/PositionHandler.h"
#include "Handler/PredictionHandler.h"
#include "Handler/RiskHandler.h"
#include "Handler/ServerEventHandler.h"
#include "Handler/TimerHandler.h"
#include "HttpHandler.h"
#include "PortfolioSubsystem.h"
#include "Util/system.h"
#include "Util/string_algorithm.h"
#include "Util/datetime.h"
#include "csv.h"
#include <fstream>
#include <mutex>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>
#include <thread>
#include "Handler/AssetHandler.h"
#include "Handler/OrderHandler.h"
#include "Handler/StrategyHandler.h"
#include "Handler/StockHandler.h"
#include "Handler/BrokerHandler.h"
#include "Handler/ExchangeHandler.h"
#include "Handler/RiskHandler.h"
#include "Handler/PortfolioHandler.h"
#include "Handler/PredictionHandler.h"
#include "Handler/FutureHandler.h"
#include "Handler/OptionHandler.h"
#include "Handler/IndexHandler.h"
#include "Handler/BackTestHandler.h"
#include "Handler/UserHandler.h"
#include "Handler/DataHandler.h"
#include "Handler/FeatureHandler.h"
#include "Handler/SectorHandler.h"
#include "Handler/ServerEventHandler.h"
#include "StrategySubSystem.h"
#include "AgentSubSystem.h"
#include "nng/nng.h"
#include "jwt-cpp/traits/nlohmann-json/traits.h"

#define THREAD_URL  "inproc://thread"
#define ERROR_RESPONSE  "not a valid request"

#define HISTORY_FILENAME  ".cmd_hist"

#define RegistHandler(name, type) \
    { type* recorder = new type(this);\
        _handlers[name] = recorder; }

#define REGIST_GET(api_name) \
_svr.Get(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Get " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    this->_handlers[api_name]->get(req, res);\
})
#define REGIST_PUT(api_name) \
_svr.Put(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Put " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    this->_handlers[api_name]->put(req, res);\
})
#define REGIST_POST(api_name) \
_svr.Post(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Post " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    this->_handlers[api_name]->post(req, res);\
})
#define REGIST_DEL(api_name) \
_svr.Delete(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Del " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    this->_handlers[api_name]->del(req, res);\
})

#define API_VERSION  "/v0"
#define API_RISK_STOP_LOSS  "/risk/stoploss"
#define API_RISK_VAR        "/risk/var"
#define API_RECORD          "/record"
#define API_ALL_STOCK       "/stocks/simple"
#define API_ALL_FUTURE      "/future/simple"
#define API_ALL_OPTION      "/option/simple"
#define API_STOCK_DETAIL    "/stocks/detail"
#define API_STOCK_HISTORY   "/stocks/history"
#define API_EXHANGE         "/exchange"
#define API_PORTFOLIO       "/portfolio"
#define API_COMMISSION      "/commission"
#define API_STRATEGY        "/strategy"
#define API_BACKTEST        "/backtest"
#define API_INDEX           "/index/quote"
#define API_MONTECARLO      "/predict/montecarlo"
#define API_FINITE_DIFF     "/predict/finite_diff"
#define API_PREDICT_OPR     "/predict/operation"
#define API_DATA_SYNC       "/data/sync"
#define API_USER_LOGIN      "/user/login"
#define API_USER_FUNDS      "/user/funds"
#define API_USER_SWITCH     "/user/switch"
#define API_SERVER_STATUS   "/server/status"
#define API_SERVER_CONFIG   "/server/config"
#define API_FEATURE         "/feature"
#define API_SECTOR_FLOW     "/stocks/sector/flow"
#define API_TRADE_ORDER     "/trade/order"
#define API_POSITION        "/position"
#define API_SERVER_EVENT    "/server/event"

void trim(std::string& input) {
  if (input.empty()) return ;

  input.erase(0, input.find_first_not_of(" "));
  input.erase(input.find_last_not_of(" ") + 1);
}

std::multimap<std::string, ContractInfo> Server::_markets;
std::map<time_t, float> Server::_inter_rates;
bool Server::_exit = false;
std::mutex Server::_sseMutex;
Map<std::thread::id, nng_socket> Server::_sseSockets;

Server::Server():_config(nullptr), _trade_exchange(nullptr), _dividends(12*60*12),
_strategySystem(nullptr), _brokerSystem(nullptr), _portfolioSystem(nullptr),
_defaultPortfolio(1), _timer(nullptr)
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
,_svr("server.crt", "server.key")
#endif
{
    for (auto mt: {MT_Shanghai, MT_Beijing, MT_Shenzhen}) {
        _working_times[mt] = std::move(GetWorkingRange(mt));
    }
    _runType = RuningType::Simualtion;
    if (!_svr.is_valid()) {
        FATAL("SSLServer initialization failed. Check your certificate and key files.");
        return ;
    }
}

Server::~Server() {
    if (_timer) {
        _timer->join();
        delete _timer;
    }
    for (auto& item: _handlers) {
        delete item.second;
    }
    if (_brokerSystem) {
        // _broker->Release();
        delete _brokerSystem;
    }
    if (_portfolioSystem) {
        delete _portfolioSystem;
    }
    if (_strategySystem) {
        delete _strategySystem;
    }
    spdlog::shutdown();
}

bool Server::Init(const char* config) {
    _config = new ServerConfig(config);
    if (!_config->IsOK()) {
      delete _config;
      return false;
    }

    InitDatabase();
    InitHandlers();
    // _mode = mode;
    return true;
}

void Server::Run() {
    Regist();
    auto port = _config->GetPort();
    INFO("Start in port {}", port);
    _svr.listen("0.0.0.0", port);
    _exit = true;
    printf("Bye\n");
}

void Server::Regist() {
    InitDefault();
    _svr.Post(API_VERSION "/exit", [this](const httplib::Request& req, httplib::Response& res) {
        // 检查权限
        if (!JWTMiddleWare(req, res)) {
            return;
        }
        INFO("Post /exit");
        // TODO: 关闭所有订单和交易, 关闭数据源

        _config->Flush();
        _svr.stop();
        });
    _svr.Post(API_VERSION API_USER_LOGIN, [this](const httplib::Request& req, httplib::Response& res) {
        INFO("Post /user/login");
        this->_handlers[API_USER_LOGIN]->post(req, res);
    });

    _svr.Get(API_VERSION API_SERVER_EVENT, [this](const httplib::Request& req, httplib::Response& res) {
        if (!JWTMiddleWare(req, res)) {
            return;
        }
        INFO("Get {}", API_SERVER_EVENT);
        this->_handlers[API_SERVER_EVENT]->get(req, res);
    });
    REGIST_POST(API_RISK_STOP_LOSS);
    REGIST_PUT(API_RISK_STOP_LOSS);
    REGIST_GET(API_RISK_STOP_LOSS);
    REGIST_DEL(API_RISK_STOP_LOSS);

    REGIST_GET(API_RECORD);
    REGIST_GET(API_STOCK_HISTORY);

    REGIST_GET(API_RISK_VAR);
    REGIST_POST(API_RISK_VAR);

    REGIST_POST(API_MONTECARLO);

    REGIST_GET(API_ALL_STOCK);

    REGIST_POST(API_EXHANGE);

    REGIST_PUT(API_PREDICT_OPR);
    REGIST_GET(API_PREDICT_OPR);
    REGIST_DEL(API_PREDICT_OPR);

    REGIST_POST(API_RECORD);
    REGIST_DEL(API_RECORD);

    REGIST_GET(API_STOCK_DETAIL);

    REGIST_PUT(API_PORTFOLIO);

    REGIST_GET(API_STRATEGY);
    REGIST_POST(API_STRATEGY);

    REGIST_POST(API_TRADE_ORDER);

    REGIST_GET(API_DATA_SYNC);
    REGIST_GET(API_SERVER_STATUS);
    REGIST_GET(API_FEATURE);
    REGIST_GET(API_INDEX);
    REGIST_GET(API_SERVER_CONFIG);
    REGIST_GET(API_SECTOR_FLOW);
    REGIST_GET(API_TRADE_ORDER);
    REGIST_GET(API_USER_FUNDS);
    REGIST_GET(API_POSITION);
    
    REGIST_POST(API_BACKTEST);
    REGIST_POST(API_SERVER_CONFIG);

    REGIST_DEL(API_TRADE_ORDER);

}

bool Server::InitDatabase() {
    auto db_path = _config->GetDatabasePath();
    InitMarket(db_path);
    // InitInterestRate(db_path + "/inter_rate.csv");

    // TODO: 获取配置中计算所需的最大数据量,然后只读取这部分数据,提升加载速度并减少内存占用
    // int prepare_count = GetMaxPrepareCount();
    // _stocks = new Stock();
    // _stocks->Init(db_path, prepare_count);
    return true;
}

void Server::InitDefault() {
    if (!_config->HasDefault())
        return;
        
    auto default_config = _config->GetDefault();
    if (!default_config.contains("broker")) {
        printf("default config require `broker`\n");
        return;
    }
    if (!default_config.contains("exchange")) {
        printf("default config require `exchange`\n");
        return;
    }
    List<String> names = default_config["exchange"];
    if (names.empty()) {
        printf("default config `exchange` is not exist.\n");
        return;
    }
    bool use_sim = false;
    for (auto& name: names) {
        auto exchange = _config->GetExchangeByName(name);
        if (exchange.empty()) {
            
        }
        auto exchanger = (ExchangeHandler*)_handlers[API_EXHANGE];
        if (!exchanger->Use(name)) {
        }
        if ((String)exchange["api"] == "sim") {
            use_sim = true;
            _runType = RuningType::Backtest;
            break;
        }
    }

    if (!use_sim) { // 开启模拟数据的情况下，不记录数据
        if (default_config.contains("record") && !default_config["record"].empty()) {
            auto recorder = (RecordHandler*)_handlers[API_RECORD];
            Set<String> symbols;
            for (auto& item: default_config["record"]) {
                if (item == "*") {
                    break;
                }
                symbols.insert((String)item);
            }
            if (!symbols.empty()) {
                recorder->SetSymbols(symbols);
            }
            recorder->StartRecord(true);
        }
    }
    
    String broker_name = default_config["broker"];
    auto broker = _config->GetBrokerByName(broker_name);
    if (broker.empty()) {
        printf("default config `broker` is not exist.\n");
        return;
    }

    _portfolioSystem = new PortfolioSubSystem(this);
    for (String porfolio: default_config["exchange"]) {
        _portfolioSystem->GetPortfolio(porfolio);
    }

    _brokerSystem = new BrokerSubSystem(this, use_sim);
    String dbpath = broker["db"];
    if (!std::filesystem::exists(dbpath)) {
        std::filesystem::create_directories(dbpath);
    }

    auto& exchagnes = ((ExchangeHandler*)_handlers[API_EXHANGE])->GetExchangesWithType();

    _brokerSystem->Init(broker, exchagnes, 1000000);

    _strategySystem = new StrategySubSystem(this);

    if (default_config.contains("strategy")) {
        auto& strategies = default_config["strategy"];
        if (strategies.contains("sim")) {
            for (String name: strategies["sim"]) {
                _strategySystem->SetupSimulation(name);
            }
        }
        if (strategies.contains("real") && strategies["real"].size() != 0) { 
            INFO("to be implement of real ");
        }
    }
    _strategySystem->Init();

    StartTimer();
}

void Server::InitStocks(const String& path) {
    static bool isInit = false;
    if (isInit)
        return;
    isInit = true;
    
    String stock_path = path + "/symbol_market.csv";
    if (!std::filesystem::exists(stock_path)) {
        // 首次启动,运行初始化脚本
        RunCommand("python tools/run_task.py 1");
        RunCommand("python tools/run_task.py 2");
    }
    io::CSVReader<2> reader(stock_path);
    reader.read_header(io::ignore_extra_column, "代码", "交易所");
    std::string code;
    std::string exch;
    while(reader.read_row(code, exch)){
        ContractInfo info;
        info._type = ContractType::AStock;
        if (exch == "SH") {
            info._exchange = MT_Shanghai;
        }
        else if (exch == "SZ") info._exchange = MT_Shenzhen;
        else if (exch == "BJ") info._exchange = MT_Beijing;
        else {
            WARN("{} Unknow exchange {}", code.c_str(), exch.c_str());
            continue;
        }
        _markets.emplace(code, std::move(info));
    }
    // 基金
    String fund_path = path + "/fund_market.csv";
    io::CSVReader<2> fund_reader(fund_path);
    String name;
    fund_reader.read_header(io::ignore_extra_column, "code", "name");
    while(fund_reader.read_row(code, name)){
        String head = code.substr(0, 2);
        String symbol = code.substr(2);
        ContractInfo info;
        info._type = ContractType::ETF;
        if (head == "sh") info._exchange = MT_Shanghai;
        else if (head == "sz") info._exchange = MT_Shenzhen;
        else {
            WARN("{} Unknow exchange {}", symbol, head);
            continue;
        }
        _markets.emplace(symbol, std::move(info));
    }
}

void Server::InitFutures(const String& path) {

}

bool Server::InitMarket(const std::string& path) {
    if (!std::filesystem::exists(path))
        return false;
    // A股
    auto defaults = _config->GetDefault();
    auto& exchangeNames = defaults["exchange"];
    for (String name: exchangeNames) {
        auto& info = _config->GetExchangeByName(name);
        if (info["type"] == "stock") {
            if (info["api"] == "sim") {
                InitStocks(path);
            } else {
                // TODO: 通过接口获取
                InitStocks(path);
            }
        }
        else if (info["type"] == "future") {
            if (info["api"] == "sim") {
                InitFutures(path);
            } else {
                // TODO: 通过接口获取
                InitFutures();
            }
        }
    }
    

    String future = path + "/future";
    const Map<String, ExchangeName> future_exc_map{
        {"czce", MT_Zhengzhou},
        {"cffex", MT_Zhongjin},
        {"dce", MT_Dalian},
        {"gfex", MT_Guangzhou},
        {"shfe", MT_ShanghaiFuture},
        {"ine", MT_ShanghaiEng},
    };
    // TODO: 通过CTP初始化获取所有有效的期货和期权代码
    // if ()
    // for (auto& files: std::filesystem::directory_iterator(future.c_str())) {
    //     if (files.is_directory())
    //         continue;

    //     auto name = files.path().filename().stem().string();
    //     List<String> tokens;
    //     split(name, tokens, "_");
    //     ContractInfo info;
    //     info._type = ContractType::Future;
    //     info._exchange = future_exc_map.at(tokens.front());
    //     _markets.emplace(tokens.back(), std::move(info));
    // }

    String option = path + "/option";
    // for (auto& files: std::filesystem::directory_iterator(option.c_str())) {
    //     if (files.is_directory())
    //         continue;
    //     auto name = files.path().filename().stem().string();
    // }
    // 指数
    // String index = path + "/index.csv";
    // io::CSVReader<2> index_reader(index);
    // index_reader.read_header(io::ignore_extra_column, "code", "name");
    // while(index_reader.read_row(code, exch)){
    //     ContractInfo info;
    //     info._type = ContractType::Index;
    //     _markets.emplace(code, std::move(info));
    // }
    return true;
}

void Server::InitMarket(const List<Pair<String, ExchangeName>>& info)
{
    for (auto& item : info) {
        ContractInfo ci;
        ci._exchange = item.second;
        auto lower_itr = _markets.lower_bound(item.first);
        auto upper_itr = _markets.upper_bound(item.first);
        if (lower_itr == upper_itr) {
            _markets.emplace(item.first, std::move(ci));
        }
        else {
            for (auto itr = lower_itr; itr != upper_itr; ++itr) {
                itr->second._exchange = item.second;
            }
        }
        
    }
}

void Server::InitFutures() {
    static bool isInit = false;
    if (isInit)
        return;

    isInit = true;
}

bool Server::InitInterestRate(const std::string& path) {
    if (!std::filesystem::exists(path))
        return false;
    io::CSVReader<2> reader(path);
    reader.read_header(io::ignore_extra_column, "报告日", "利率");
    std::string str_date;
    std::string rate;
    while(reader.read_row(str_date, rate)){
        auto date = FromStr(str_date);
        _inter_rates[date] = atof(rate.c_str());
    }
    return true;
}

double Server::GetFreeRate(time_t t) {
    if (_inter_rates.count(t))
        return _inter_rates[t];

    auto itr = _inter_rates.upper_bound(t);
    if (itr != _inter_rates.end())
        return itr->second;
    
    return _inter_rates.lower_bound(t)->second;
}

IStopLoss* Server::GetStopLoss(const String& name) {
    auto risk = (StopLossHandler*)_handlers[API_RISK_STOP_LOSS];
    return risk->Switch(name);
}

HttpHandler* Server::GetHandler(const String& name)
{
  return _handlers[name];
}

void Server::RegistAPI() {

}

ExchangeName Server::GetExchange(const std::string& symbol) {
    auto lower_itr = _markets.lower_bound(symbol);
    auto upper_itr = _markets.upper_bound(symbol);
    if (lower_itr == upper_itr)
        return ExchangeName::MT_Unknow;

    Set<ExchangeName> types;
    for (auto itr = lower_itr; itr != upper_itr; ++itr) {
        types.insert(itr->second._exchange);
    }
    if (types.size() > 1) {
        WARN("symbol {} has {} exhanges. please check!", symbol, types.size());
    }
    return *types.begin();
}

ExchangeInterface* Server::GetAvaliableStockExchange()
{
    auto handler = (ExchangeHandler*)(_handlers[API_EXHANGE]);
    auto name = handler->GetActiveStock();
    if (name.empty())
        return nullptr;
    auto& exchanges = handler->GetExchanges();
    return exchanges.at(name);
}

ExchangeInterface* Server::GetAvaliableFutureExchange()
{
    auto handler = (ExchangeHandler*)(_handlers[API_EXHANGE]);
    auto name = handler->GetActiveFuture();
    if (name.empty())
        return nullptr;
    auto& exchanges = handler->GetExchanges();
    return exchanges.at(name);
}

ContractType Server::GetContractType(const std::string& symbol, const String& exhange)
{
    auto lower_itr = _markets.lower_bound(symbol);
    auto upper_itr = _markets.upper_bound(symbol);
    if (lower_itr == upper_itr)
        return ContractType::AStock;

    Set<ContractType> types;
    for (auto itr = lower_itr; itr != upper_itr; ++itr) {
        types.insert(itr->second._type);
    }
    if (exhange.empty() || types.size() == 1) {
        return *types.begin();
    } else {
        return *types.begin();
    }
}

float Server::GetInterestRate(time_t datetime) {
    auto itr = _inter_rates.find(datetime);
    if (itr == _inter_rates.end())
        return -1;

    return itr->second;
}

bool Server::LoadDataBySymbol(const String& symbol, StockAdjustType right, DataFrequencyType freq) {
    auto& dataCache = (right == StockAdjustType::None? _data: _hfqdata);
    if (dataCache.count(symbol)) {
        // TODO: 检查数据最后一天的时间是否是当天,如果不是,需要加载新的数据进来
        return true;
    }
    String path = _config->GetDatabasePath();
    auto type = GetContractType(symbol);
    String contract = (right == StockAdjustType::None? "Astock": "A_hfq");
    String filename;
    try {
        auto& df = dataCache[symbol];
        _symbolCache.push_front(symbol);
        switch (type)
        {
        case ContractType::ETF:
            contract = "etf";
            if (freq == DataFrequencyType::Min5) {
                filename = symbol + "_5_data.csv";
            }
            else {
                filename = symbol + "_hist_data.csv";
            }
            path += "/" + contract + "/" + filename;
            if (!LoadStock(df, path))
                return false;
            break;
        case ContractType::LOF:
            contract = "lof";
            break;
        case ContractType::Future:
            contract = "Future";
            path += "/" + contract + "/" + filename;
            if (!LoadFuture(df, path))
                return false;
            break;
        case ContractType::Option:
        case ContractType::AsianOption:
        case ContractType::BarrierOption:
        case ContractType::BinaryOption:
        case ContractType::EureanOption:
            contract = "Option";
            break;
        default:
            if (freq == DataFrequencyType::Min5) {
                contract = "A_tick";
            }
            filename = symbol + "_hist_data.csv";
            path += "/" + contract + "/" + filename;
            if (!LoadStock(df, path))
                return false;
            break;
        }
    }
    catch (std::exception& e) {
        WARN("read csv file fail:{}", e.what());
        return false;
    }

    if (_symbolCache.size() > MAX_HISTORY_SIZE) {
        auto erase_symbol = _symbolCache.back();
        dataCache.erase(erase_symbol);
        _symbolCache.pop_back();
    }
    return true;
}

bool Server::LoadStock(DataFrame& df, const String& path) {
    if (!std::filesystem::exists(path))
        return false;

    String datetime;
    double open, close, high, low, volumn, amount, price_volatility, change_percent, turnover_rate;

    Vector<String> sv;
    df.load_column("datetime", sv);
    Vector<double> dv;
    for (auto name : { "open", "close", "high","low", "volume", "amount", "volatility", "change", "turnover",
        }) {
        df.load_column(name, dv);
    }
    uint32_t index = 0;
    io::CSVReader<10> reader(path);
    // 日期,开盘,收盘,最高,最低,成交量,成交额,振幅,涨跌幅,涨跌额,换手率
    reader.read_header(io::ignore_extra_column, "datetime", "open", "close", "high", "low", "volume", "amount", "volatility", "change", "turnover");
    while (reader.read_row(datetime, open, close, high, low, volumn, amount, price_volatility, change_percent,
        turnover_rate)) {
        auto t = FromStr(datetime);
        df.append_row(&index, std::make_pair("datetime", t), std::make_pair("open", open), std::make_pair("close", close),
            std::make_pair("high", high), std::make_pair("low", low), std::make_pair("volume", volumn), std::make_pair("amount", amount),
            std::make_pair("volatility", price_volatility), std::make_pair("change", change_percent), std::make_pair("turnover", turnover_rate)
        );
        ++index;
    }
    return true;
}

bool Server::LoadStock(DataFrame& df, symbol_t symbol, int lastN) {
    auto filename = get_symbol(symbol) + "_hist_data.csv";
    String path = _config->GetDatabasePath();
    path += "/Astock/" + filename;

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        WARN("Can't open file: {}", path);
        return false;
    }
    // 获取文件大小并初始化位置指针
    std::streampos pos = file.tellg();
    int newlineCount = 0;
    char ch;
    // 从文件末尾向前搜索换行符
    while (pos > 0) {
        pos -= 1;
        file.seekg(pos, std::ios::beg); // 向前移动一个字节
        file.get(ch);
        
        if (ch == '\n') {
            if (++newlineCount == lastN) 
                break; // 找到第n个换行符
        }
    }
    // 如果未找到足够换行符，回到文件开头
    
    if (pos != 0) {
        pos += 1;
        file.seekg(pos); // 跳过找到的换行符
    }
    else file.seekg(0);

    // 读取目标行内容
    uint32_t index = 0;
    std::string line;
    bool visited_head = false;
    df.load_column("datetime", Vector<time_t>());
    for (auto name : { "open", "close", "high", "low", "volume"}) {
        df.load_column(name, Vector<double>());
    }
    while (std::getline(file, line)) {
        // 处理换行符（\r\n）
        if (!line.empty() && line.back() == '\n') {
            line.pop_back();
            if (line.back() == '\r') {
                line.pop_back();
            }
        }
        
        if (!visited_head && line.find("open") != std::string::npos) {
            visited_head = true;
            continue;
        }

        Vector<String> content;
        split(line, content, ",");

        auto t = FromStr(content[0]);
        auto open = atof(content[1].c_str());
        auto close = atof(content[2].c_str());
        auto low = atof(content[3].c_str());
        auto high = atof(content[4].c_str());
        auto volumn = atof(content[5].c_str());
        df.append_row(&index, std::make_pair("datetime", t), std::make_pair("open", open), std::make_pair("close", close),
            std::make_pair("high", high), std::make_pair("low", low), std::make_pair("volume", volumn)
        );
        ++index;
    }
    file.close();
    return true;
}

bool Server::LoadFuture(DataFrame& df, const String& path) {
    if (!std::filesystem::exists(path))
        return false;

    String datetime;
    double open, close, high, low, volumn;

    Vector<String> sv;
    df.load_column("datetime", sv);
    Vector<double> dv;
    for (auto name : { "open", "close", "high","low", "volume"}) {
        df.load_column(name, dv);
    }
    uint32_t index = 0;
    io::CSVReader<6> reader(path);
    // 日期,开盘,收盘,最高,最低,成交量
    reader.read_header(io::ignore_extra_column, "datetime", "open", "close", "high", "low", "volume");
    while (reader.read_row(datetime, open, close, high, low, volumn)) {
        auto t = FromStr(datetime);
        df.append_row(&index, std::make_pair("datetime", t), std::make_pair("open", open), std::make_pair("close", close),
            std::make_pair("high", high), std::make_pair("low", low), std::make_pair("volume", volumn)
        );
        ++index;
    }
    return true;
}

void Server::StartTimer()
{
    _timer = new std::thread(&Server::Timer, this);
}

void Server::Timer()
{
    SetCurrentThreadName("ScheduleTImer");
    using Clock = std::chrono::steady_clock;
    using Duration = Clock::duration;
    using TimePoint = Clock::time_point;
    Duration interval(std::chrono::seconds(5));
    TimePoint next_wake = Clock::now() + interval;
    nng_socket sock;
    Publish(URI_SERVER_EVENT, sock);
    if (_runType == RuningType::Backtest) {
        while(!_exit) {
            auto handler = (ExchangeHandler*)(_handlers[API_EXHANGE]);
            TimerWorker(sock);
            for (auto exchange: handler->GetExchanges()) {
                if (exchange.second->IsLogin()) {
                    exchange.second->QueryQuotes();
                    next_wake = Clock::now() + interval;
                } else {
                    std::this_thread::sleep_until(next_wake);
                    next_wake += interval;
                }
            }
        }
    } else {
        while(!_exit) {
            std::this_thread::sleep_until(next_wake);
            if (_exit) {
                nng_close(sock);
                return;
            }

            TimerWorker(sock);
            auto fut = std::async(std::launch::async,  [this]() {
                auto curr = Now();
                Schedules(curr);
                // 
                UpdateQuoteQueryStatus(curr);
                //
                _dividends.Update();
            });
            next_wake += interval;
        }
    }
    nng_close(sock);
}

void Server::TimerWorker(nng_socket sock) {
    nlohmann::json status;
#ifndef _WIN32
    if (get_system_status(status)) {
        double cpu = status["cpu"];
        Pair<double, double> val = status["mem"];
        String info = format_sse("system_status", { {"cpu", std::to_string(cpu)}, {"mem", std::to_string(val.first)}, {"total", std::to_string(val.second)}});
        nng_send(sock, info.data(), info.size(), 0);
    }
#endif
    // 更新持仓
    auto broker = GetAvaliableStockExchange();
    AccountPosition ap;
    broker->GetPosition(ap);
    for (auto& item : ap._positions) {
        Map<String, String> data;
        data["id"] = get_symbol(item._symbol);
        data["price"] = std::to_string(item._price);
        data["curPrice"] = std::to_string(item._curPrice);
        data["name"] = to_utf8(item._name);
        data["quantity"] = std::to_string(item._holds);
        data["valid_quantity"] = std::to_string(item._validHolds);
        String info = format_sse("update_position", data);
        nng_send(sock, info.data(), info.size(), 0);
    }
    // 更新订单
    OrderList ol;
    if (broker->GetOrders(ol) && !ol.empty()) {
        nlohmann::json array;
        for (auto& item: ol) {
            nlohmann::json order;
            order["id"] = get_symbol(item._symbol);

            array.push_back(std::move(order));
        }
        Map<String, String> data;
        data["data"] = array.dump();
        String info = format_sse("update_order", data);
        nng_send(sock, info.data(), info.size(), 0);
    }
}

// void Server::SendCloseFeatures() {
    // nng_socket send;
    // if (!Publish(URI_RAW_QUOTE, send)) {
    //     WARN("regist SendCloseFeatures socket fail.");
    //     return;
    // }
    // auto featureSystem = _strategySystem->GetFeatureSystem();
    // auto symbols = featureSystem->GetFeatureSymbols();
    // static constexpr std::size_t flags = yas::mem|yas::binary;
    // for (auto symbol: symbols) {
    //     if (is_stock(symbol)) {
    //         DataFrame df;
    //         String path = _config->GetDatabasePath() + "/" + get_symbol(symbol) + "_hist_data.csv";
    //         if (!LoadStock(df, path)) {
    //             WARN("Load stock {} data fail.", get_symbol(symbol));
    //             continue;
    //         }
    //         auto& close = df.get_column<double>("close");
    //         if (close.empty())
    //             continue;

    //         QuoteInfo info;
    //         info._symbol = symbol;
    //         info._time = FromStr(df.get_column<String>("datetime").back());
    //         info._open = df.get_column<double>("open").back();
    //         info._high = df.get_column<double>("high").back();
    //         info._volume = df.get_column<double>("volume").back();
    //         info._close = close.back();
    //         info._low = df.get_column<double>("low").back();
            
    //         yas::shared_buffer buf = yas::save<flags>(info);
    //         if (0 != nng_send(send, buf.data.get(), buf.size, 0)) {
    //             WARN("send daily close quote message fail.");
    //             return;
    //         }
    //     }
    // }
    // nng_close(send);
// }

void Server::UpdateQuoteQueryStatus(time_t curr) {
    auto handler = (ExchangeHandler*)(_handlers[API_EXHANGE]);
    for (auto exchange: handler->GetExchanges()) {
        if (exchange.second->IsWorking(curr)) {
            if (exchange.second->IsLogin()) {
                exchange.second->QueryQuotes();
            }
            else {
                exchange.second->Login();
            }
        } else {
            exchange.second->StopQuery();
        }
    }
}

void Server::Schedules(time_t t) {
    // start script 
    std::tm *ltm = localtime(&t);
    static int prev_day = -1;
    static bool daily_once = false;
    if (prev_day == -1) {
        prev_day = ltm->tm_wday;
    }
    if (prev_day != ltm->tm_wday) {
        daily_once = false;
        prev_day = ltm->tm_wday;
    }
    auto time = _config->GetDailyTime();
    if (!daily_once && ltm->tm_hour == time.first && ltm->tm_min == time.second) { // 20:00(default) run once
        daily_once = true;
        LOG("run once script");
        RunCommand("cd ../tools && python daily.py");

        if (prev_day == 6) {
            // every week 6 run once
            RunCommand("cd ../tools && python compress_ctp.py ../data/zh ./zh.tar.gz");
        }

        // TODO: run daily forecast with newest data
        // SendCloseFeatures();
    }
}

std::shared_ptr<DataGroup> Server::PrepareData(const Set<symbol_t>& symbols, DataFrequencyType type, StockAdjustType right) {
    Map<contract_type, List<String>> all_symbol;
    for (auto sym: symbols) {
        all_symbol[sym._type].emplace_back(get_symbol(sym));
    }
    for (auto& item: all_symbol) {
        if (item.first == contract_type::stock) {
            return PrepareStockData(item.second, type, right);
        } else {
            WARN("not implement for data type {}", (int)item.first);
        }
    }
    return nullptr;
}

std::shared_ptr<DataGroup> Server::PrepareStockData(const List<String>& symbols, DataFrequencyType type, StockAdjustType right) {
    for (auto& sym: symbols) {
        auto itr = _data.find(sym);
        DataFrame* curr_df = nullptr;
        if (itr == _data.end()) {
            if (!LoadDataBySymbol(sym, right, type)) {
                continue;
            }
        }
    }
    std::shared_ptr<DataGroup> handle;
    if (!symbols.empty()) {
        handle = std::make_shared<DataGroup>(symbols, _data);
    }
    return handle;
}

ExchangeInterface* Server::GetExchange(ExchangeType type) {
    ExchangeHandler* handler = (ExchangeHandler*)_handlers[API_EXHANGE];
    return handler->GetExchangeByType(type);
}

int Server::GetMaxPrepareCount() {
    auto& schemas = _config->GetSchemas();
    int max_count = 50;
    for (auto& schema: schemas) {
        if (schema.count("prepare")) {
            int prepare = schema["prepare"];
            max_count = std::max(prepare, max_count);
        }
    }
    return max_count;
}

void Server::InitHandlers() {
    RegistHandler(API_EXHANGE, ExchangeHandler);
    RegistHandler(API_RECORD, RecordHandler);
    RegistHandler(API_RISK_STOP_LOSS, StopLossHandler);
    RegistHandler(API_ALL_STOCK, StockHandler);
    RegistHandler(API_STOCK_DETAIL, StockDetailHandler);
    RegistHandler(API_STOCK_HISTORY, StockHistoryHandler);
    RegistHandler(API_RISK_VAR, VaRHandler);
    RegistHandler(API_PORTFOLIO, PortfolioHandler);
    RegistHandler(API_MONTECARLO, MonteCarloHandler);
    RegistHandler(API_ALL_FUTURE, FutureHandler);
    RegistHandler(API_ALL_OPTION, OptionHandler);
    RegistHandler(API_STRATEGY, StrategyHandler);
    RegistHandler(API_INDEX, IndexHandler);
    RegistHandler(API_BACKTEST, BackTestHandler);
    RegistHandler(API_PREDICT_OPR, PredictionHandler);
    RegistHandler(API_TRADE_ORDER, OrderHandler);
    RegistHandler(API_USER_LOGIN, UserLoginHandler);
    RegistHandler(API_USER_FUNDS, UserFundHandler);
    RegistHandler(API_DATA_SYNC, DataSyncHandler);
    RegistHandler(API_SERVER_STATUS, ServerStatusHandler);
    RegistHandler(API_SERVER_CONFIG, SystemConfigHandler);
    RegistHandler(API_FEATURE, FeatureHandler);
    RegistHandler(API_SECTOR_FLOW, SectorHandler);
    RegistHandler(API_SERVER_EVENT, ServerEventHandler);
    RegistHandler(API_POSITION, PositionHandler);
    RegistHandler(API_USER_SWITCH, UserSwitchHandler);

    //StopLossHandler* risk = (StopLossHandler*)_handlers[API_RISK_STOP_LOSS];
    //risk->doWork({});
}

AccountPosition& Server::GetPosition(const String& account) {
    return _account_positions[account];
}

Set<String> Server::GetAccounts() {
    Set<String> accs;
    for (auto& item: _account_positions) {
        accs.insert(item.first);
    }
    return accs;
}

Vector<double> Server::GetDailyClosePrice(symbol_t symbol, int N, StockAdjustType adjust) {
    Vector<double> ret;
    if (is_stock(symbol)) {
        DataFrame df;
        if (!LoadStock(df, symbol, N)) {
            return ret;
        }
        if (adjust == StockAdjustType::After) {

        } else {
            auto& close = df.get_column<double>("close");
            int min_size = std::min((int)close.size(), N);
            for (int i = min_size - 1; i >= 0; --i) {
                ret.insert(ret.begin(), close[i]);
            }
        }
    }
    return ret;
}

bool Server::GetDividendInfo(symbol_t symbol, Map<time_t, DividendData>& dividends_info) {
    auto path = _config->GetDatabasePath();
    path += "/dividend/" + get_symbol(symbol) + "_dividend.csv";
    if (!std::filesystem::exists(path)) {
        WARN("{} dividend info lost.", get_symbol(symbol));
        return false;
    }

    std::ifstream ifs;
    ifs.open(path.c_str());
    if (!ifs.is_open())
        return false;

    
    String line;
    bool is_header = true;
    // 1, 2, 3, 5, 6 - 送股,转增,派息,实施,登记日
    while (std::getline(ifs, line)) {
        if (line.empty())
            break;
        if (is_header) {
            is_header = false;
            continue;
        }

        Vector<String> content;
        split(line, content, ",");
        
        DividendData data;
        data._bonus = atof(content[1].c_str());
        data._divd = atof(content[3].c_str());
        data._transf = atof(content[2].c_str());
        data._start = FromStr(content[5]);
    }
    ifs.close();
    return true;
}

double Server::AdjustAfter(symbol_t symbol, double org_price, time_t org_t) {
    if (!is_stock(symbol))
        return org_price;

    if (!_dividends.Exist(symbol)) {
        if (!GetDividendInfo(symbol, _dividends[symbol])) {
            return org_price;
        }
    }

    Map<time_t, DividendData>& dividends_info = _dividends[symbol];
    
    auto litr = dividends_info.lower_bound(org_t);
    for (auto itr = dividends_info.begin(); itr != litr; ++itr) {
        org_price += itr->second._divd;
    }
    return org_price;
}

double Server::AdjustBefore(symbol_t symbol, double org_price, time_t org_t) {
    if (!is_stock(symbol))
        return org_price;

    if (!_dividends.Exist(symbol)) {
        if (!GetDividendInfo(symbol, _dividends[symbol])) {
            return org_price;
        }
    }
    Map<time_t, DividendData>& dividends_info = _dividends[symbol];
    
    for (auto itr = dividends_info.upper_bound(org_t); itr != dividends_info.end(); ++itr) {
        org_price -= itr->second._divd;
    }

    return org_price;
}

double Server::ResetPrice(symbol_t symbol, double adj_price, time_t adj_t) {
    if (!is_stock(symbol))
        return adj_price;

    Map<time_t, DividendData> dividends_info;
    if (!GetDividendInfo(symbol, dividends_info)) {
        return adj_price;
    }
    auto litr = dividends_info.lower_bound(adj_t);
    for (auto itr = dividends_info.begin(); itr != litr; ++itr) {
        adj_price -= itr->second._divd;
    }
    return adj_price;
}

bool Server::IsOpen(symbol_t symbol, time_t t) {
    auto exc_type = Server::GetExchange(get_symbol(symbol));
    return IsOpen(exc_type, t);
}

bool Server::IsOpen(ExchangeName exchange, time_t t) {
    if (_runType == RuningType::Backtest)
        return true;
    
    auto& working = _working_times[exchange];
    for (auto& tr: working) {
        if (tr == t) {
            return true;
        }
    }
    return false;
}

time_t Server::GetCloseTime(ExchangeName exchange) {
    time_t t = Now();
    auto& working = _working_times[exchange];
    for (auto& tr: working) {
        if (tr == t) {
            struct tm *timeinfo = localtime(&t);
            int end_in_day = tr.End();
            timeinfo->tm_hour = end_in_day / 3600;
            timeinfo->tm_min = (end_in_day % 3600) / 60;
            timeinfo->tm_sec = ((end_in_day % 3600) % 60) / 60;
            return mktime(timeinfo);
        }
    }
    return 0;
}

bool Server::SendEmail(const String& content) {
    auto sender = _config->GetSMTPSender();
    auto pwd = _config->GetSMTPPasswd();

    String prefix = "python tool/mail.py ";
    prefix += sender + " " + pwd;

    String cmd = prefix + " " + _config->GetWarningAddr() + " \"" + content + "\"";
    try {
        return RunCommand(cmd);
    } catch (const std::exception& e) {
        WARN("send email fail: {}", e.what());
        return false;
    } catch (...) {
        WARN("send email fail, unknow reason.");
        return false;
    }
}

bool Server::JWTMiddleWare(const httplib::Request& req, httplib::Response& res) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    auto auth_header = req.get_header_value("Authorization");
    if (auth_header.empty()) {
        res.status = 401;
        res.set_content("{'error': 'Missing or invalid Authorization header.'}","application/json");
        return false;
    }
    try {
        // 3. 验证和解析Token
        using traits = jwt::traits::nlohmann_json;
        auto verifier = jwt::verify<traits>()
            .allow_algorithm(jwt::algorithm::rs256(_config->GetPublicKey(), "", "", "")) // 验证签名算法和密钥
            .with_issuer(_config->GetIssuer());                 // 验证签发者是否匹配
        auto decoded = jwt::decode<traits>(auth_header);
        verifier.verify(decoded); // 如果验证失败（如签名无效、过期），会抛出异常
    } catch (const std::exception& e) {
        // 其他解析异常
        FATAL("{} {}", _config->GetPublicKey(), e.what());
        res.status = 401;
        res.set_content(std::string("{\"error\": \"Token processing error: ") + e.what() + "\"}", "application/json");
        return false;
    }
#endif
    return true;
}

nng_socket Server::GetSocket() {
    auto id = std::this_thread::get_id();
    std::unique_lock<std::mutex> lock(_sseMutex);
    auto itr = _sseSockets.find(id);
    if (itr == _sseSockets.end()) {
        nng_socket sock;
        Publish(URI_SERVER_EVENT, sock);
        _sseSockets[id] = sock;
        return sock;
    }
    return _sseSockets[id];
}
