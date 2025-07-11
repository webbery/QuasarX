#include "server.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include "Bridge/exchange.h"
#include "BrokerSubSystem.h"
#include "RiskSubSystem.h"
#include "DataFrame/DataFrameTypes.h"
#include "Handler/PredictionHandler.h"
#include "Handler/RiskHandler.h"
#include "Handler/TimerHandler.h"
#include "HttpHandler.h"
#include "PortfolioSubsystem.h"
#include "Util/log.h"
#include "Util/system.h"
#include "json.hpp"
#include "Util/string_algorithm.h"
#include "Util/datetime.h"
#include "csv.h"
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>
#include "Handler/AccountHandler.h"
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
#include "StrategySubSystem.h"
#include "TraderSubsystem.h"
#include "AgentSubSystem.h"
#include "nng/nng.h"

#define THREAD_URL  "inproc://thread"
#define ERROR_RESPONSE  "not a valid request"

#define HISTORY_FILENAME  ".cmd_hist"

#define RegistHandler(name, type) \
    { type* recorder = new type(this);\
        _handlers[name] = recorder; }

#define REGIST_GET(api_name) \
_svr.Get(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    LOG("Get " API_VERSION api_name);\
    this->_handlers[api_name]->get(req, res);\
})
#define REGIST_PUT(api_name) \
_svr.Put(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    LOG("Put " API_VERSION api_name);\
    this->_handlers[api_name]->put(req, res);\
})
#define REGIST_POST(api_name) \
_svr.Post(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    LOG("Post " API_VERSION api_name);\
    this->_handlers[api_name]->post(req, res);\
})
#define REGIST_DEL(api_name) \
_svr.Delete(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    LOG("Del " API_VERSION api_name);\
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

void trim(std::string& input) {
  if (input.empty()) return ;

  input.erase(0, input.find_first_not_of(" "));
  input.erase(input.find_last_not_of(" ") + 1);
}

nng_mtx * job_lock;
nng_socket req_sock;

std::multimap<std::string, ContractInfo> Server::_markets;
std::map<time_t, float> Server::_inter_rates;

Server::Server():_config(nullptr), _trade_exchange(nullptr), 
_exit(false), _strategySystem(nullptr), _brokerSystem(nullptr), _portfolioSystem(nullptr),
_defaultPortfolio(1), _timer(nullptr), _riskSystem(nullptr), _traderSystem(nullptr) {

}

Server::~Server() {
    _exit = true;
    if (_timer) {
        _timer->join();
        delete _timer;
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
    if (_riskSystem) {
        delete _riskSystem;
    }
    if (_traderSystem) {
        delete _traderSystem;
    }
}

bool Server::Init(const char* config) {
    _config = new ServerConfig(config);
    if (!_config->IsOK()) {
      delete _config;
      return false;
    }

    InitDatabase();
    InitHandlers();
    
    _strategySystem = new StrategySubSystem(this);
    _strategySystem->Init();
    // _mode = mode;
    return true;
}

void Server::Run() {
    // if (_mode == MODE_COMMAND) {
    //   // TODO:启动定时器,按照配置的时间启动交易服务;测试直接运行
    // //   int cnt = 0;
    // //   for (auto ex : _exchanges) {
    // //     if (!ex->Login()) {
    // //         cnt++;
    // //     }
    // //   }
    // //   if (cnt == _exchanges.size())
    // //     return;

    //   RunCammand();
    //   printf("Bye\n");
    // } else {
        Regist();
        auto port = _config->GetPort();
        INFO("Start in port {}", port);
        _svr.listen("0.0.0.0", port);
        printf("Bye\n");

        for (auto& item: _handlers) {
            delete item.second;
        }
    // }
}

void Server::Regist() {
    InitDefault();
    _svr.Post(API_VERSION "/exit", [this](const httplib::Request& req, httplib::Response& res) {
        LOG("Post /exit");
        // TODO:检查权限
        // TODO: 关闭所有订单和交易, 关闭数据源

        _config->Flush();
        _svr.stop();
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

    _svr.Post(API_VERSION API_RECORD, [this](const httplib::Request& req, httplib::Response& res) {
        LOG("Post /v0/record");
        this->_handlers[API_RECORD]->post(req, res);
        });
    _svr.Delete(API_VERSION API_RECORD, [this](const httplib::Request& req, httplib::Response& res) {
        LOG("Del /v0/record");
        this->_handlers[API_RECORD]->del(req, res);
        });

    _svr.Get(API_VERSION API_STOCK_DETAIL, [this](const httplib::Request& req, httplib::Response& res) {
        LOG("Get " API_VERSION API_STOCK_DETAIL);
        this->_handlers[API_STOCK_DETAIL]->get(req, res);
        });

    _svr.Put(API_VERSION API_PORTFOLIO, [this](const httplib::Request& req, httplib::Response& res) {
        LOG("Put " API_VERSION API_PORTFOLIO);
        this->_handlers[API_PORTFOLIO]->put(req, res);
        });

    REGIST_GET(API_STRATEGY);
    REGIST_POST(API_STRATEGY);
}

bool Server::InitDatabase() {
    auto db_path = _config->GetDatabasePath();
    InitMarket(db_path);
    InitInterestRate(db_path + "/inter_rate.csv");

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
    for (auto& name: names) {
        auto exchange = _config->GetExchangeByName(name);
        if (exchange.empty()) {
            
        }
        auto exchanger = (ExchangeHandler*)_handlers[API_EXHANGE];
        if (!exchanger->Use(name)) {
            return;
        }
    }

    StartTimer();

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

    String broker_name = default_config["broker"];
    auto broker = _config->GetBrokerByName(broker_name);
    if (broker.empty()) {
        printf("default config `broker` is not exist.\n");
        return;
    }

    _portfolioSystem = new PortfolioSubSystem(this);
    if (default_config.contains("portfolio")) {
        _defaultPortfolio = default_config["portfolio"];
        if (!_portfolioSystem->HasPortfolio(_defaultPortfolio)) {
            WARN("portfolio {} not exist.", _defaultPortfolio);
        }
    }
    _portfolioSystem->Start();

    _brokerSystem = new BrokerSubSystem(this, (ExchangeHandler*)_handlers[API_EXHANGE]);
    String dbpath = broker["db"];
    if (!std::filesystem::exists(dbpath)) {
        std::filesystem::create_directories(dbpath);
    }
    auto real_path = dbpath + "/" + broker_name + ".db";
    _brokerSystem->Init(real_path.c_str());

    // Risk system start
    _riskSystem = new RiskSubSystem(this);
    if (default_config.contains("risk")) {
        _riskSystem->Init(default_config["risk"]);
    }
    _riskSystem->Start();

    _traderSystem = new TraderSystem(this, dbpath);
    if (default_config.contains("strategy")) {
        auto& strategies = default_config["strategy"];
        if (strategies.contains("sim")) {
            for (String name: strategies["sim"]) {
                _traderSystem->SetupSimulation(name);
            }
        }
        if (strategies.contains("real")) {
            INFO("to be implement of real ");
        }
    }
    _traderSystem->Start();
}

bool Server::InitMarket(const std::string& path) {
    if (!std::filesystem::exists(path))
        return false;
    // A股
    String stock_path = path + "/symbol_market.csv";
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

    String future = path + "/future";
    const Map<String, ExchangeName> future_exc_map{
        {"czce", MT_Zhengzhou},
        {"cffex", MT_Zhongjin},
        {"dce", MT_Dalian},
        {"gfex", MT_Guangzhou},
        {"shfe", MT_ShanghaiFuture},
        {"ine", MT_ShanghaiEng},
    };
    for (auto& files: std::filesystem::directory_iterator(future.c_str())) {
        if (files.is_directory())
            continue;

        auto name = files.path().filename().stem().string();
        List<String> tokens;
        split(name, tokens, "_");
        ContractInfo info;
        info._type = ContractType::Future;
        info._exchange = future_exc_map.at(tokens.front());
        _markets.emplace(tokens.back(), std::move(info));
    }

    String option = path + "/option";
    // for (auto& files: std::filesystem::directory_iterator(option.c_str())) {
    //     if (files.is_directory())
    //         continue;
    //     auto name = files.path().filename().stem().string();
    // }
    // 指数
    String index = path + "/index.csv";
    io::CSVReader<2> index_reader(index);
    index_reader.read_header(io::ignore_extra_column, "code", "name");
    while(index_reader.read_row(code, exch)){
        ContractInfo info;
        info._type = ContractType::Index;
        _markets.emplace(code, std::move(info));
    }
    return true;
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

// void Server::RunCammand() {
//     linenoiseInstallWindowChangeHandler();
//     linenoiseHistoryLoad(HISTORY_FILENAME);
//     linenoiseSetCompletionCallback([](char const* prefix, linenoiseCompletions* lc) {

//     });
//     auto commander = RegistCommand();
//     InitDefault();

//     do {
//         char* result = linenoise("trash> ");
//         if (result == nullptr) break;
//         std::string input(result);
//         free(result);
//         trim(input);
//         if (input == "exit") {
//             break;
//         }
//         if (input.empty()) continue;

//         // 命令格式: /xxx/xxx param1=xxx param2=xxx ...
//         commander->run(input);
//         linenoiseHistoryAdd(input.c_str());
//     } while (true);
//     delete commander;
//     linenoiseHistorySave(HISTORY_FILENAME);
//     linenoiseHistoryFree();
// }

// Commander* Server::RegistCommand() {
//     Commander* cmder = new Commander();
//     //cmder->route("/account", new AccountHandler(this));
//     //cmder->route("/asset", new AssetHandler(this));
//     //cmder->route("/order", new OrderHandler(this));
//     //cmder->route("/strategy", new StrategyHandler(this));
//     //cmder->route(API_STOCK, new StockHandler(this));
//     // _recorder = new RecordHandler(this);
//     // cmder->route(API_RECORD, _recorder);
//     // _exchanger = new ExchangeHandler(this);
//     // cmder->route(API_EXHANGE, _exchanger);
//     // if (!_risker) {
//     //     _risker = new RiskHandler(this);
//     // }
//     // cmder->route("/risk", _risker);
//     return cmder;
// }

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

bool Server::LoadDataBySymbol(const String& symbol, DataRightType right, DataFrequencyType freq) {
    auto& dataCache = (right == DataRightType::None? _data: _hfqdata);
    if (dataCache.count(symbol)) {
        // TODO: 检查数据最后一天的时间是否是当天,如果不是,需要加载新的数据进来
        return true;
    }
    String path = _config->GetDatabasePath();
    auto type = GetContractType(symbol);
    String contract = (right == DataRightType::None? "Astock": "A_hfq");
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
    while(!_exit) {
        std::this_thread::sleep_until(next_wake);
        if (_exit)
            return;

        auto fut = std::async(std::launch::async,  [this]() {
            auto curr = Now();
            // 
            UpdateQuoteQueryStatus(curr);
            Schedules(curr);
        });
        next_wake += interval;
    }
}

void Server::UpdateQuoteQueryStatus(time_t curr) {
    auto handler = (ExchangeHandler*)(_handlers[API_EXHANGE]);
    for (auto exchange: handler->GetExchanges()) {
        if (exchange.second->IsWorking(curr)) {
            if (exchange.second->IsLogin()) {
                exchange.second->QueryQuotes();
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
    if (!daily_once && ltm->tm_hour == 20 && ltm->tm_min == 0) { // 20:00 run once
        daily_once = true;
        LOG("run once script");
        RunCommand("cd ../tools && python daily.py");

        if (prev_day == 6) {
            // every week 6 run once
            RunCommand("cd ../tools && python compress_ctp.py ../data/zh ./zh.tar.gz");
        }
    }
}

std::shared_ptr<DataGroup> Server::PrepareData(const Set<symbol_t>& symbols, DataFrequencyType type, DataRightType right) {
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

std::shared_ptr<DataGroup> Server::PrepareStockData(const List<String>& symbols, DataFrequencyType type, DataRightType right) {
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

    StopLossHandler* risk = (StopLossHandler*)_handlers[API_RISK_STOP_LOSS];
    risk->doWork({});
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

