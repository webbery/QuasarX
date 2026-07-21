#include "server.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include "Bridge/exchange.h"
#include "Bridge/SIM/ETFHistorySimulation.h"
#include "BrokerSubSystem.h"
#include "DataFrame/DataFrameTypes.h"
#include "ExchangeManager.h"
#include "Handler/PositionHandler.h"
#include "Handler/PredictionHandler.h"
#include "Handler/RiskHandler.h"
#include "RiskSubSystem.h"
#include "Handler/ServerEventHandler.h"
#include "Handler/RecordHandler.h"
#include "Handler/ReplayHandler.h"
#include "HttpHandler.h"
#include "PortfolioSubsystem.h"
#include "StrategyNode.h"
#include "Util/QuoteDB.h"
#include "Util/system.h"
#include "Util/string_algorithm.h"
#include "Util/datetime.h"
#include "boost/algorithm/string/join.hpp"
#include "csv.h"
#include <fstream>
#include <nng/protocol/reqrep0/rep.h>
#include <nng/protocol/reqrep0/req.h>
#include <nng/supplemental/util/platform.h>
#include <stdexcept>
#include "Handler/AssetHandler.h"
#include "Handler/VolatilityHandler.h"
#include "Handler/SignalHandler.h"
#include "Handler/PCAHandler.h"
#include "Handler/CUSUMHandler.h"
#include "Handler/OrderHandler.h"
#include "Handler/NavHandler.h"
#include "Handler/StrategyHandler.h"
#include "Handler/StockHandler.h"
#include "Handler/StrategyLogHandler.h"
#include "Handler/NodeIOHandler.h"
#include "Handler/PythonRunnerHandler.h"
#include "Handler/XGBoostHandler.h"
#include "Handler/QuoteDownloadHandler.h"
#include "Handler/QuoteDataHandler.h"
#include "Handler/FinanceHandler.h"
#include "Handler/FinanceDataHandler.h"
#include "Handler/DividendHandler.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/SignalNode.h"
#include "Nodes/PortfolioNode.h"
#include "Nodes/ExecuteNode.h"
#include "Handler/BrokerHandler.h"
#include "Handler/RiskHandler.h"
#include "Handler/PortfolioHandler.h"
#include "Handler/PredictionHandler.h"
#include "Handler/FutureHandler.h"
#include "Handler/OptionHandler.h"
#include "Handler/IndexHandler.h"
#include "Handler/BackTestHandler.h"
#include "Handler/CapacityHandler.h"
#include "Handler/RiskStatusHandler.h"
#include "Handler/UserHandler.h"
#include "Handler/DataHandler.h"
#include "Handler/FeatureHandler.h"
#include "Handler/SectorHandler.h"
#include "Handler/SectorQuoteHandler.h"
#include "Handler/ServerEventHandler.h"
#include "Handler/CapitalRiskHandler.h"
#include "Handler/StrategyRiskHandler.h"
#include "Handler/ShiborHandler.h"
#include "Handler/MacroHandler.h"
#include "StrategySubSystem.h"
#include "AgentSubSystem.h"
#include "Util/FinanceDB.h"
#include "Util/DailyDecision.h"
#include "nng/nng.h"
#include "jwt-cpp/traits/nlohmann-json/traits.h"
#include <boost/algorithm/string.hpp>

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
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    this->_handlers[api_name]->get(req, res);\
})
#define REGIST_PUT(api_name) \
_svr.Put(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Put " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    this->_handlers[api_name]->put(req, res);\
})
#define REGIST_POST(api_name) \
_svr.Post(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Post " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    this->_handlers[api_name]->post(req, res);\
})
#define REGIST_DEL(api_name) \
_svr.Delete(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Del " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    this->_handlers[api_name]->del(req, res);\
})

#define API_VERSION  "/v0"
#define API_RISK_STOP_LOSS  "/risk/stoploss"
#define API_RISK_VAR        "/risk/var"
#define API_RECORD          "/record"
#define API_REPLAY          "/replay"
#define API_ALL_FUTURE      "/future/simple"
#define API_ALL_OPTION      "/option/simple"
#define API_OPTION_HISTORY  "/option/history"
#define API_OPTION_DETAIL   "/option/detail"
#define API_STOCK_DETAIL    "/stocks/detail"
#define API_STOCK_HISTORY   "/stocks/history"
#define API_ALL_STOCK       "/stocks/simple"
#define API_STOCK_PRIVILEGE "/stocks/privilege"
#define API_STOCK_PARAMS    "/stocks/params"
#define API_EXHANGE         "/exchange"
#define API_PORTFOLIO       "/portfolio"
#define API_COMMISSION      "/commission"
#define API_STRATEGY        "/strategy"
#define API_STRATEGY_NODES  "/strategy/nodes"
#define API_STRATEGY_NODE   "/strategy/node"
#define API_BACKTEST        "/backtest"
#define API_CAPACITY        "/capacity"
#define API_RISK_STATUS     "/risk/status"
#define API_RISK_RESET      "/risk/reset-breaker"
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
#define API_SECTOR_QUOTE    "/stocks/sector/quote"
#define API_SHIBOR          "/market/shibor"
#define API_TRADE_ORDER     "/trade/order"
#define API_TRADE_HISTORY   "/trade/history"
#define API_POSITION        "/position"
#define API_SERVER_EVENT    "/server/event"
#define API_RISK_CAPITAL    "/risk/capital"
#define API_RISK_DAILY      "/risk/daily"
#define API_RISK_CLOSEALL   "/risk/closeall"
#define API_RISK_STRATEGIES "/risk/strategies"
#define API_NAV_HISTORY     "/nav/history"
#define API_VOLATILITY      "/analysis/volatility"
#define API_CUSUM           "/analysis/cusum"
#define API_SIGNAL          "/analysis/signal"
#define API_PCA             "/analysis/pca"
#define API_CAPITAL_STATUS  "/server/capital"
#define API_STRATEGY_LOGS   "/strategy/logs"
#define API_NODE_IO         "/node/io"
#define API_PYTHON_RUNNER   "/python/run"
#define API_QUOTE           "/quote"
#define API_QUOTE_DATA      "/quote/data"
#define API_FINANCE         "/finance"
#define API_FINANCE_DATA    "/finance/data"
#define API_DIVIDEND        "/dividend"
#define API_XGBOOST         "/xgboost"

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

Server::Server():_config(nullptr), _dividends(12*60*12),
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
    if (_exchangeMgr) {
        delete _exchangeMgr;
    }
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
    if (_riskSystem) {
        delete _riskSystem;
    }
    spdlog::shutdown();
}

bool Server::Init(const char* config) {
    _config = new ServerConfig(config);
    if (!_config->IsOK()) {
      delete _config;
      return false;
    }

    InitHandlers();
    InitDatabase();

    // 创建 ExchangeManager 协调器
    _exchangeMgr = new ExchangeManager(this);
    _exchangeMgr->StartQuoteDispatcher();
    // _mode = mode;
    return true;
}

void Server::Run() {
    Regist();
    auto port = _config->GetPort();
    INFO("Start in port {}", port);
    if (!_svr.listen("0.0.0.0", port)) {
        INFO("listen fail: {}", port);
    }
    _exit = true;
    printf("Bye\n");
    _exchangeMgr->StopQuoteDispatcher();
    _exchangeMgr->Shutdown();
    _strategySystem->Release();
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

    // 资金风控 API
    REGIST_GET(API_RISK_CAPITAL);
    REGIST_POST(API_RISK_CAPITAL);
    REGIST_GET(API_RISK_DAILY);
    REGIST_POST(API_RISK_DAILY);
    REGIST_POST(API_RISK_CLOSEALL);
    REGIST_GET(API_RISK_STRATEGIES);

    REGIST_POST(API_RISK_STOP_LOSS);
    REGIST_PUT(API_RISK_STOP_LOSS);
    REGIST_GET(API_RISK_STOP_LOSS);
    REGIST_DEL(API_RISK_STOP_LOSS);

    REGIST_GET(API_RECORD);
    REGIST_GET(API_STOCK_HISTORY);
    REGIST_GET(API_TRADE_HISTORY);

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

    REGIST_POST(API_REPLAY);

    REGIST_GET(API_STOCK_DETAIL);

    REGIST_PUT(API_PORTFOLIO);

    REGIST_GET(API_STRATEGY);
    REGIST_POST(API_STRATEGY);
    REGIST_DEL(API_STRATEGY);

    REGIST_POST(API_TRADE_ORDER);

    REGIST_GET(API_DATA_SYNC);
    REGIST_GET(API_SERVER_STATUS);
    REGIST_GET(API_FEATURE);
    REGIST_GET(API_INDEX);
    REGIST_GET(API_SERVER_CONFIG);
    REGIST_GET(API_SECTOR_FLOW);
    REGIST_GET(API_SECTOR_QUOTE);
    REGIST_GET(API_SHIBOR);
    REGIST_GET(API_MACRO);
    REGIST_GET(API_TRADE_ORDER);
    REGIST_GET(API_USER_FUNDS);
    REGIST_GET(API_POSITION);
    REGIST_GET(API_STRATEGY_NODES);
    REGIST_GET(API_STRATEGY_NODE);
    REGIST_GET(API_STRATEGY_LOGS);
    REGIST_DEL(API_STRATEGY_LOGS);
    REGIST_GET(API_NODE_IO);
    REGIST_DEL(API_NODE_IO);
    REGIST_POST(API_PYTHON_RUNNER);
    REGIST_POST(API_QUOTE);
    REGIST_GET(API_QUOTE);
    REGIST_DEL(API_QUOTE);
    REGIST_POST(API_QUOTE_DATA);
    REGIST_DEL(API_QUOTE_DATA);
    REGIST_POST(API_FINANCE);
    REGIST_GET(API_FINANCE);
    REGIST_DEL(API_FINANCE);
    REGIST_POST(API_FINANCE_DATA);
    REGIST_DEL(API_FINANCE_DATA);
    REGIST_POST(API_DIVIDEND);
    REGIST_GET(API_DIVIDEND);
    REGIST_DEL(API_DIVIDEND);
    REGIST_POST(API_XGBOOST);
    REGIST_GET(API_STOCK_PRIVILEGE);
    REGIST_GET(API_STOCK_PARAMS);
    REGIST_GET(API_OPTION_HISTORY);
    REGIST_GET(API_CAPITAL_STATUS);

    REGIST_POST(API_BACKTEST);
    REGIST_POST(API_CAPACITY);
    REGIST_GET(API_RISK_STATUS);
    REGIST_POST(API_RISK_RESET);
    REGIST_GET(API_VOLATILITY);
    REGIST_POST(API_CUSUM);
    REGIST_GET(API_SIGNAL);
    REGIST_GET(API_PCA);
    REGIST_POST(API_SERVER_CONFIG);

    REGIST_PUT(API_STRATEGY_NODE);
    REGIST_PUT(API_STOCK_PARAMS);

    REGIST_DEL(API_TRADE_ORDER);
    REGIST_PUT(API_TRADE_ORDER);

}

bool Server::InitDatabase() {
    auto db_path = _config->GetDatabasePath();
    InitMarket(db_path);

    // TODO: 获取配置中计算所需的最大数据量,然后只读取这部分数据,提升加载速度并减少内存占用
    // int prepare_count = GetMaxPrepareCount();
    // _stocks = new Stock();
    // _stocks->Init(db_path, prepare_count);
    return true;
}

void Server::InitDefault() {
    // 初始化 QuoteDB（使用正确的数据库路径）
    auto& quoteDB = QuoteDB::instance();
    auto db_path = _config->GetDatabasePath();
    if (!quoteDB.isInitialized()) {
        if (quoteDB.init(db_path + "/quote")) {
            INFO("[InitDefault] QuoteDB initialized at {}/quote", db_path);
        } else {
            WARN("[InitDefault] Failed to initialize QuoteDB at {}/quote", db_path);
        }
    }

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
    INFO("[InitDefault] Found {} exchange(s) in default config", names.size());
    bool use_sim = false;
    for (auto& name: names) {
        INFO("[InitDefault] Processing exchange: {}", name);
        auto exchange = _config->GetExchangeByName(name);
        if (exchange.empty()) {
            WARN("[InitDefault] Exchange config not found for name: {}", name);
            continue;
        }
        INFO("[InitDefault] Calling _exchangeMgr->Use({})", name);
        if (!_exchangeMgr->Use(name)) {
            WARN("[InitDefault] Failed to register exchange: {}", name);
            return;
        }
        INFO("[InitDefault] Successfully registered exchange: {}", name);
        if ((String)exchange["api"] == STOCK_HISTORY_SIM) {
            use_sim = true;
            _runType = RuningType::Backtest;
            break;
        }
        else if ((String)exchange["api"] == STOCK_REAL_SIM) {
            _runType = RuningType::Simualtion;
            break;
        }
    }

    // 解析 etf.t0/etf.t1 配置并传递给 ETFHistorySimulation
    if (_exchangeMgr) {
        _exchangeMgr->ConfigureETFExchange();
    }

    if (!use_sim) { // 开启模拟数据的情况下，不记录数据
        if (default_config.contains("record") && !default_config["record"].empty()) {
            auto recorder = (RecordHandler*)_handlers[API_RECORD];
            Set<String> symbols;
            bool recordAll = false;
            for (auto& item: default_config["record"]) {
                if (item == "*") {
                    recordAll = true;
                    break;
                }
                symbols.insert((String)item);
            }
            if (recordAll) {
                // "*" 表示全记录：不设置过滤集合，RecordHandler 记录所有收到的 tick
                recorder->SetSymbols({});
                recorder->StartRecord(true);
            } else if (!symbols.empty()) {
                // 指定 symbol 列表：只记录匹配的 tick
                recorder->SetSymbols(symbols);
                recorder->StartRecord(true);
            }
            // 空数组 []：不启动记录（RecordHandler 对象仍由 _handlers 持有，正常析构）
        }
        _runType = RuningType::Real;
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

    auto& exchagnes = _exchangeMgr->GetExchangesByType();

    _brokerSystem->Init(broker, exchagnes);
    
    // 初始化资金池
    double initialCapital = 1000000;  // 默认 100 万
    if (default_config.contains("capitalRisk") && default_config["capitalRisk"].contains("initialCapital")) {
        initialCapital = default_config["capitalRisk"]["initialCapital"].get<double>();
    }
    String capitalPersistPath = dbpath + "/capital_pool.json";
    _brokerSystem->initCapitalPool(initialCapital, capitalPersistPath);

    // 初始化风控子系统
    _riskSystem = new RiskSubSystem(this);
    nlohmann::json riskConfig = nlohmann::json::object();
    if (default_config.contains("risk")) {
        riskConfig = default_config["risk"];
    }
    _riskSystem->Init(riskConfig);

    // 关联资金风控 Handler 与 RiskSubSystem 中的 CapitalRiskManager
    if (_riskSystem && _riskSystem->GetCapitalRiskManager()) {
        auto capitalRiskManager = _riskSystem->GetCapitalRiskManager();
        // 设置到 Handler
        auto capitalHandler = (CapitalRiskHandler*)_handlers[API_RISK_CAPITAL];
        if (capitalHandler) {
            capitalHandler->SetCapitalRiskManager(capitalRiskManager);
        }
        auto dailyHandler = (DailyLossRiskHandler*)_handlers[API_RISK_DAILY];
        if (dailyHandler) {
            dailyHandler->SetCapitalRiskManager(capitalRiskManager);
        }
        auto closeHandler = (CloseAllPositionHandler*)_handlers[API_RISK_CLOSEALL];
        if (closeHandler) {
            closeHandler->SetCapitalRiskManager(capitalRiskManager);
        }
    }

    _strategySystem = new StrategySubSystem(this);

    if (default_config.contains("strategy")) {
        auto& strategies = default_config["strategy"];
        if (strategies.contains(STOCK_HISTORY_SIM)) {
            for (String name: strategies[STOCK_HISTORY_SIM]) {
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

void Server::AddSymbolToMarket(const String& code, ContractInfo&& info) {
    _markets.emplace(code, std::move(info));
}

std::pair<bool, String> Server::ValidateStrategyConfig(const nlohmann::json& config) {
    try {
        auto lambda_delete = [] (const List<QNode*>& nodes) {
            // 清理节点
            for (auto* node : nodes) {
                delete node;
            }
        };
        // 1. 解析策略图
        auto nodes = parse_strategy_script_v2(config, this);

        // 2. 验证图的完整性：必须包含 Input/Signal/Portfolio/Execution 节点
        // 统计各类节点数量
        int inputCount = 0, signalCount = 0, portfolioCount = 0, executionCount = 0;
        for (auto* node : nodes) {
            if (dynamic_cast<QuoteInputNode*>(node)) inputCount++;
            else if (dynamic_cast<SignalNode*>(node)) signalCount++;
            else if (dynamic_cast<PortfolioNode*>(node)) portfolioCount++;
            else if (dynamic_cast<ExecuteNode*>(node)) executionCount++;
        }

        // 检查必需节点
        List<String> missingNodes;
        if (inputCount == 0) missingNodes.push_back("Input(行情数据输入)");
        if (signalCount == 0) missingNodes.push_back("Signal(信号节点)");
        if (portfolioCount == 0) missingNodes.push_back("Portfolio(投资组合节点)");
        if (executionCount == 0) missingNodes.push_back("Execution(执行器节点)");

        if (!missingNodes.empty()) {
            lambda_delete(nodes);
            String info = boost::algorithm::join(missingNodes, ", ");
            String errorMsg = std::format("strategy graph not correct, {} is lost", info);
            WARN("{}", errorMsg);
            return {false, errorMsg};
        }

        // 3. 验证图的连通性: 从 Input 节点出发，BFS 遍历检查是否能到达 Signal、Portfolio、Execution
        Set<QNode*> visited;
        Vector<QNode*> queue;

        // 找到所有 Input 节点作为起点
        for (auto* node : nodes) {
            if (dynamic_cast<QuoteInputNode*>(node)) {
                if (!visited.count(node)) {
                    visited.insert(node);
                    queue.push_back(node);
                }
            }
        }

        // BFS 遍历
        size_t head = 0;
        while (head < queue.size()) {
            auto* current = queue[head++];
            const auto& outs = current->outs();
            for (const auto& [handle, nextNode] : outs) {
                if (!visited.count(nextNode)) {
                    visited.insert(nextNode);
                    queue.push_back(nextNode);
                }
            }
        }

        // 检查是否有未访问的节点(不连通)
        if (visited.size() != nodes.size()) {
            lambda_delete(nodes);
            String errorMsg = std::format("策略图存在孤立的节点，总节点数={}，可到达节点数={}", 
                nodes.size(), visited.size());
            WARN("{}", errorMsg);
            return {false, errorMsg};
        }

        // 4. 检查 Input 是否能到达 Signal/Portfolio/Execution
        bool canReachSignal = false, canReachPortfolio = false, canReachExecution = false;
        for (auto* node : visited) {
            if (dynamic_cast<SignalNode*>(node)) canReachSignal = true;
            else if (dynamic_cast<PortfolioNode*>(node)) canReachPortfolio = true;
            else if (dynamic_cast<ExecuteNode*>(node)) canReachExecution = true;
        }

        List<String> unreachableNodes;
        if (!canReachSignal) unreachableNodes.push_back("Signal");
        if (!canReachPortfolio) unreachableNodes.push_back("Portfolio");
        if (!canReachExecution) unreachableNodes.push_back("Execution");

        if (!unreachableNodes.empty()) {
            lambda_delete(nodes);
            String info = boost::algorithm::join(unreachableNodes, ",");
            String errorMsg = std::format("Input 节点无法到达: {}", info);
            WARN("{}", errorMsg);
            return {false, errorMsg};
        }

        // 5. 清理临时节点
        lambda_delete(nodes);

        INFO("策略图验证通过: {} Input, {} Signal, {} Portfolio, {} Execution",
            inputCount, signalCount, portfolioCount, executionCount);

        return {true, ""};
    } catch (const std::runtime_error& e) {
        return {false, e.what()};
    } catch (const std::exception& e) {
        return {false, e.what()};
    }
}

void Server::InitStocks(const String& path) {
    static bool isInit = false;
    if (isInit)
        return;
    isInit = true;

    // Fallback: 如果 _markets 为空，尝试从 Exchange 重新加载
    if (_markets.empty()) {
        WARN("Symbol market is empty, trying to reload from exchange...");

        auto exchange = _exchangeMgr ? _exchangeMgr->GetExchangeByType(ExchangeType::EX_HX) : nullptr;
        if (exchange) {
            List<SymbolInfo> symbols;
            if (exchange->GetAllStockSymbols(symbols)) {
                for (auto& sym : symbols) {
                    ContractInfo info;
                    info._type = sym._type;
                    info._exchange = sym._exchange;
                    info._market = sym._market;
                    info._name = sym._name;
                    _markets.emplace(sym._code, std::move(info));
                }
                INFO("Loaded {} stock symbols (fallback)", symbols.size());
            }

            if (exchange->GetAllFundSymbols(symbols)) {
                for (auto& sym : symbols) {
                    ContractInfo info;
                    info._type = sym._type;
                    info._exchange = sym._exchange;
                    info._name = sym._name;
                    _markets.emplace(sym._code, std::move(info));
                }
                INFO("Loaded {} fund symbols (fallback)", symbols.size());
            }

            if (exchange->GetAllOptionSymbols(symbols)) {
                for (auto& sym : symbols) {
                    ContractInfo info;
                    info._type = sym._type;
                    info._exchange = sym._exchange;
                    info._name = sym._name;
                    info._expireDate = sym._expireDate;
                    info._deliveryDate = sym._deliveryDate;
                    info._strike = sym._strike;
                    _markets.emplace(sym._code, std::move(info));
                }
                INFO("Loaded {} option symbols (fallback)", symbols.size());
            }
        }
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
            if (info["api"] == TICKFLOW_QUOTE_API) {
                // 通过 TickFlow 接口获取标的列表
                auto exchange = _exchangeMgr ? _exchangeMgr->GetExchangeByType(ExchangeType::EX_HX) : nullptr;
                if (exchange) {
                    List<SymbolInfo> symbols;
                    if (exchange->GetAllStockSymbols(symbols)) {
                        for (auto& sym : symbols) {
                            ContractInfo ci;
                            ci._type = sym._type;
                            ci._exchange = sym._exchange;
                            ci._name = sym._name;
                            _markets.emplace(sym._code, std::move(ci));
                        }
                        INFO("Loaded {} stock symbols from TickFlow", symbols.size());
                    } else {
                        WARN("Failed to load stock symbols from TickFlow, fallback to local data");
                        InitStocks(path);
                    }
                }
            } else {
                // STOCK_HISTORY_SIM 等其他模式：标的同步已在 RegisterExchange 中完成，无需重复加载
            }
        }
        else if (info["type"] == "future") {
            if (info["api"] == FEATURE_HISTORY_SIM) {
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

Pair<ContractType, char> Server::GetContractType(const std::string& symbol, const String& exhange)
{
    auto lower_itr = _markets.lower_bound(symbol);
    auto upper_itr = _markets.upper_bound(symbol);
    if (lower_itr == upper_itr)
        return { ContractType::AStock, 1 }; // A股默认看涨股票

    Set<Pair<ContractType, char>> types;
    for (auto itr = lower_itr; itr != upper_itr; ++itr) {
        char is_call = itr->second._type >> 7;
        Pair<ContractType, char> info{ ConvertContractType(itr->second._type), is_call };
        types.emplace(std::move(info));
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
    Pusher(URI_SERVER_EVENT, sock);
    if (_runType == RuningType::Backtest) {
        while(!_exit) {
            TimerWorker(sock);
            auto curr = Now();
            // 
            UpdateQuoteQueryStatus(curr);
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
        nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
    }
#endif
    // 更新持仓
    AccountPosition ap;
    _exchangeMgr->GetTradingPosition(ap);
    nlohmann::json holds;
    for (auto& item : ap._positions) {
        nlohmann::json position;
        position["id"] = get_symbol(item._symbol);
        position["price"] = std::to_string(item._price);
        position["curPrice"] = std::to_string(item._curPrice);
        position["name"] = to_utf8(item._name);
        position["quantity"] = std::to_string(item._holds);
        position["valid_quantity"] = std::to_string(item._validHolds);
        holds.emplace_back(std::move(position));
    }
    if (!ap._positions.empty()) {
        Map<String, String> data;
        data["data"] = holds.dump();
        String info = format_sse("update_position", data);
        nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
    }

    // 更新订单
    OrderList ol;
    if (_exchangeMgr->GetTradingOrders(SecurityType::Stock, ol) && !ol.empty()) {
        nlohmann::json array;
        for (auto& item: ol) {
            nlohmann::json order;
            String strSymbol = get_symbol(item._symbol);
            order["id"] = strSymbol;
            order["price"] = item._price;
            order["sysID"] = item._sysID;
            order["status"] = (int)item._status;
            order["direct"] = (int)item._side;
            order["quantity"] = item._volume;
            order["orderType"] = item._type;
            order["timestamp"] = item._time;
            order["name"] = GetName(strSymbol);
            array.push_back(std::move(order));
        }
        Map<String, String> data;
        data["data"] = array.dump();
        String info = format_sse("update_order", data);
        nng_send(sock, info.data(), info.size(), NNG_FLAG_NONBLOCK);
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
    _exchangeMgr->UpdateQuoteQueryStatus(curr);
}

void Server::Schedules(time_t t) {
    // start script
    std::tm *ltm = localtime(&t);
    static int prev_day = -1;
    static bool daily_once = false;
    static bool daily_init_done = false;
    static bool daily_force_done = false;
    if (prev_day == -1) {
        prev_day = ltm->tm_wday;
    }
    if (prev_day != ltm->tm_wday) {
        daily_once = false;
        daily_init_done = false;
        daily_force_done = false;
        prev_day = ltm->tm_wday;
    }

    // 15:00 初始化日级策略执行（收盘数据写入后由 TickFlowBridge 触发 MarkSymbolReady）
    if (!daily_init_done && ltm->tm_hour == 15 && ltm->tm_min == 0) {
        daily_init_done = true;
        if (_strategySystem) {
            _strategySystem->InitDailyExecution();
            _strategySystem->ResetDaily();
            INFO("[Schedules] Daily execution initialized at 15:00");
        }
    }

    // 15:30 超时兜底：强制执行所有未完成的策略
    if (!daily_force_done && ltm->tm_hour == 15 && ltm->tm_min == 30) {
        daily_force_done = true;
        if (_strategySystem) {
            _strategySystem->ForceExecuteAllDaily();
            INFO("[Schedules] Force executed all daily strategies at 15:30");
        }
    }

    // 20:00 更新分红数据（为第二天准备）
    auto time = _config->GetDailyTime();
    if (!daily_once && ltm->tm_hour == time.first && ltm->tm_min == time.second) { // 20:00(default) run once
        daily_once = true;
        LOG("run once script");
        RunCommand("cd ../tools && python daily.py");

        // 更新分红除权数据：从活跃策略收集标的 → 下载 → 导入 DuckDB
        if (_strategySystem) {
            Set<String> allSymbols;
            auto names = _strategySystem->GetStrategyNames();
            for (auto& name : names) {
                auto pools = _strategySystem->GetPools(name);
                for (auto sym : pools) {
                    allSymbols.insert(get_symbol(sym));
                }
            }
            if (!allSymbols.empty()) {
                String symbolsStr = boost::algorithm::join(allSymbols, ",");
                INFO("[Schedules] Updating dividends for {} symbols from {} strategies",
                     allSymbols.size(), names.size());
                String cmd = "cd ../tools && python fetch_dividend_data.py \""
                           + symbolsStr + "\" --download --data-dir data";
                RunCommand(cmd);
                // 导入 CSV 到 finance.db
                String dbPath = _config->GetDatabasePath();
                FinanceDB::instance().importAllDividends(dbPath + "/dividend");
            }
        }

        if (prev_day == 6) {
            // every week 6 run once
            //RunCommand("cd ../tools && python compress_ctp.py ../data/zh ./zh.tar.gz");
            // 每周末更新一次
            RunCommand("cd ../tools && python run_task.py 3");
            ReloadMarketData(_config->GetDatabasePath());
        }

        // TODO: run daily forecast with newest data
        // SendCloseFeatures();
    }
}

ExchangeInterface* Server::GetExchange(ExchangeType type) {
    return _exchangeMgr->GetExchangeByType(type);
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

void Server::ReloadMarketData(const String& path) {
    _markets.clear();

    // 优先通过 Exchange 接口获取符号信息
    auto exchange = _exchangeMgr ? _exchangeMgr->GetExchangeByType(ExchangeType::EX_HX) : nullptr;
    if (exchange) {
        List<SymbolInfo> symbols;

        // 加载股票
        if (exchange->GetAllStockSymbols(symbols)) {
            for (auto& sym : symbols) {
                ContractInfo info;
                info._type = sym._type;
                info._exchange = sym._exchange;
                info._market = sym._market;
                info._name = sym._name;
                _markets.emplace(sym._code, std::move(info));
            }
            INFO("Reloaded {} stock symbols", symbols.size());
        }

        // 加载基金
        symbols.clear();
        if (exchange->GetAllFundSymbols(symbols)) {
            for (auto& sym : symbols) {
                ContractInfo info;
                info._type = sym._type;
                info._exchange = sym._exchange;
                info._name = sym._name;
                _markets.emplace(sym._code, std::move(info));
            }
            INFO("Reloaded {} fund symbols", symbols.size());
        }

        // 加载期权
        symbols.clear();
        if (exchange->GetAllOptionSymbols(symbols)) {
            for (auto& sym : symbols) {
                ContractInfo info;
                info._type = sym._type;
                info._exchange = sym._exchange;
                info._name = sym._name;
                info._expireDate = sym._expireDate;
                info._deliveryDate = sym._deliveryDate;
                info._strike = sym._strike;
                _markets.emplace(sym._code, std::move(info));
            }
            INFO("Reloaded {} option symbols", symbols.size());
        }
    }
}

void Server::InitHandlers() {
    RegistHandler(API_RECORD, RecordHandler);
    RegistHandler(API_REPLAY, ReplayHandler);
    RegistHandler(API_RISK_STOP_LOSS, StopLossHandler);
    RegistHandler(API_RISK_CAPITAL, CapitalRiskHandler);
    RegistHandler(API_RISK_DAILY, DailyLossRiskHandler);
    RegistHandler(API_RISK_CLOSEALL, CloseAllPositionHandler);
    RegistHandler(API_RISK_STRATEGIES, StrategyRiskHandler);
    RegistHandler(API_ALL_STOCK, StockHandler);
    RegistHandler(API_STOCK_DETAIL, StockDetailHandler);
    RegistHandler(API_STOCK_HISTORY, StockHistoryHandler);
    RegistHandler(API_PORTFOLIO, PortfolioHandler);
    RegistHandler(API_MONTECARLO, MonteCarloHandler);
    RegistHandler(API_ALL_FUTURE, FutureHandler);
    RegistHandler(API_ALL_OPTION, OptionHandler);
    RegistHandler(API_OPTION_HISTORY, OptionHistoryHandler);
    RegistHandler(API_STRATEGY, StrategyHandler);
    RegistHandler(API_STRATEGY_NODES, StrategyNodesHandler);
    RegistHandler(API_STRATEGY_NODE, StrategyNodeHandler);
    RegistHandler(API_INDEX, IndexHandler);
    RegistHandler(API_BACKTEST, BackTestHandler);
    RegistHandler(API_CAPACITY, CapacityHandler);
    RegistHandler(API_RISK_STATUS, RiskStatusHandler);
    RegistHandler(API_RISK_RESET, RiskStatusHandler);
    RegistHandler(API_PREDICT_OPR, PredictionHandler);
    RegistHandler(API_TRADE_ORDER, OrderHandler);
    RegistHandler(API_TRADE_HISTORY, HistoryTradeHandler);
    RegistHandler(API_NAV_HISTORY, NavHandler);
    RegistHandler(API_USER_LOGIN, UserLoginHandler);
    RegistHandler(API_USER_FUNDS, UserFundHandler);
    RegistHandler(API_DATA_SYNC, DataSyncHandler);
    RegistHandler(API_SERVER_STATUS, ServerStatusHandler);
    RegistHandler(API_SERVER_CONFIG, SystemConfigHandler);
    RegistHandler(API_FEATURE, FeatureHandler);
    RegistHandler(API_SECTOR_FLOW, SectorHandler);
    RegistHandler(API_SECTOR_QUOTE, SectorQuoteHandler);
    RegistHandler(API_SHIBOR, ShiborHandler);
    RegistHandler(API_MACRO, MacroHandler);
    RegistHandler(API_SERVER_EVENT, ServerEventHandler);
    RegistHandler(API_POSITION, PositionHandler);
    RegistHandler(API_USER_SWITCH, UserSwitchHandler);
    RegistHandler(API_STOCK_PRIVILEGE, StockPrivilege);
    RegistHandler(API_STOCK_PARAMS, StockParams);
    RegistHandler(API_VOLATILITY, VolatilityHandler);
    RegistHandler(API_CUSUM, CUSUMHandler);
    RegistHandler(API_SIGNAL, SignalHandler);
    RegistHandler(API_PCA, PCAHandler);
    RegistHandler(API_STRATEGY_LOGS, StrategyLogHandler);
    RegistHandler(API_NODE_IO, NodeIOHandler);
    RegistHandler(API_PYTHON_RUNNER, PythonRunnerHandler);
    RegistHandler(API_QUOTE, QuoteDownloadHandler);
    RegistHandler(API_QUOTE_DATA, QuoteDataHandler);
    RegistHandler(API_FINANCE, FinanceHandler);
    RegistHandler(API_FINANCE_DATA, FinanceDataHandler);
    RegistHandler(API_DIVIDEND, DividendHandler);
    RegistHandler(API_XGBOOST, XGBoostHandler);

    //StopLossHandler* risk = (StopLossHandler*)_handlers[API_RISK_STOP_LOSS];
    //risk->doWork({});

    // 关联资金风控 Handler 与 RiskSubSystem 中的 CapitalRiskManager
    // 注意：RiskSubSystem 在 FlowSubsystem 中初始化，需要在 InitDefault 中关联
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

// Vector<double> Server::GetDailyClosePrice(symbol_t symbol, int N, StockAdjustType adjust) {
//     Vector<double> ret;
//     if (is_stock(symbol)) {
//         DataFrame df;
//         if (!LoadStock(df, symbol, N)) {
//             return ret;
//         }
//         if (adjust == StockAdjustType::After) {

//         } else {
//             auto& close = df.get_column<double>("close");
//             int min_size = std::min((int)close.size(), N);
//             for (int i = min_size - 1; i >= 0; --i) {
//                 ret.insert(ret.begin(), close[i]);
//             }
//         }
//     }
//     return ret;
// }

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
    if (pwd.empty() || sender.empty())
        return false;

    String scriptFile("tool/mail.py");
    if (!std::filesystem::exists(scriptFile))
        return false;
    String prefix = "python " + scriptFile +" ";
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
        Pusher(URI_SERVER_EVENT, sock);
        _sseSockets[id] = sock;
        return sock;
    }
    return _sseSockets[id];
}

ExchangeName Server::GetExchangeName(const String& prefix) {
    ExchangeName exchange = ExchangeName::MT_Unknow;
    if (prefix == "sz") {
        exchange = ExchangeName::MT_Shenzhen;
    }
    else if (prefix == "sh") {
        exchange = ExchangeName::MT_Shanghai;
    }
    else if (prefix == "bj") {
        exchange = ExchangeName::MT_Beijing;
    }
    return exchange;
}

String Server::GetName(const String& symbol)
{
    Vector<String> tokens;
    split(symbol, tokens, ".");
    String key = tokens.back();

    auto exchange = GetExchangeName(tokens.front());

    auto lower_itr = _markets.lower_bound(key);
    auto upper_itr = _markets.upper_bound(key);
    for (; lower_itr != upper_itr; ++lower_itr) {
        if (exchange == ExchangeName::MT_Unknow) {
            return lower_itr->second._name;
        }
        else {
            if (exchange == lower_itr->second._exchange)
                return lower_itr->second._name;
        }
    }
    return "Newest";
}

const ContractInfo& Server::GetSecurity(const String& symbol) {
    Vector<String> tokens;
    split(symbol, tokens, ".");
    String key = tokens.back();
    auto exchange = GetExchangeName(tokens.front());
    auto lower_itr = _markets.lower_bound(key);
    auto upper_itr = _markets.upper_bound(key);
    for (; lower_itr != upper_itr; ++lower_itr) {
        if (exchange == ExchangeName::MT_Unknow) {
            return lower_itr->second;
        }
        else {
            if (exchange == lower_itr->second._exchange)
                return lower_itr->second;
        }
    }
    auto str = fmt::format("symbol {} not find", symbol);
    throw std::runtime_error(str.c_str());
}

ExchangeInfo Server::GetExchangeInfo(const String& name) {
    auto& config = GetConfig();
    auto exchange = config.GetExchangeByName(name);

    std::string quote_addr = exchange.value("quote", "");
    std::string trade_addr = exchange.value("trade", "");
    ExchangeInfo handle;
    strcpy(handle._local_addr, config.GetHost().c_str());

    // 兼容没有 quote/trade 字段的配置（如 tickflow-quote 纯行情 Bridge）
    std::vector<std::string> trade_info;
    if (!trade_addr.empty()) {
        split(trade_addr, trade_info, ":");
    }
    std::vector<std::string> quote_info;
    if (!quote_addr.empty()) {
        split(quote_addr, quote_info, ":");
    }

    if (!quote_info.empty()) {
        strcpy(handle._quote_addr, quote_info[0].c_str());
    }
    if (!trade_info.empty()) {
        strcpy(handle._default_addr, trade_info[0].c_str());
    }
    strcpy(handle._productID, config.GetProductID().c_str());
    if (exchange.contains("account")) {
        std::string username = exchange["account"];
        strcpy(handle._username, username.c_str());
    }
    if (exchange.contains("username")) {
        std::string username = exchange["username"];
        strcpy(handle._username, username.c_str());
    }
    if (exchange.contains("passwd")) {
        std::string passwd = exchange["passwd"];
        strncpy(handle._passwd, passwd.data(), std::min(32, (int)passwd.size()));
    }
    if (exchange.contains("option")) {  
        std::string option_addr = exchange["option"];
        std::vector<std::string> option_info;
        split(option_addr, option_info, ":");
        strcpy(handle._option_addr, option_info[0].c_str());
        handle._option_port = atoi(option_info[1].c_str());
    }
    if (trade_info.size() > 1) {
        handle._stock_port = atoi(trade_info[1].c_str());
    }
    if (quote_info.size() > 1) {
        handle._quote_port = atoi(quote_info[1].c_str());
    }
    auto accounts = config.GetStockAccounts();
    if (accounts.size() > 0) {// 回测模式可以不用支持用户帐号
        auto account = accounts.front().first;
        auto accpwd = accounts.front().second;
        strcpy(handle._account, account.c_str());
        strcpy(handle._accpwd, accpwd.c_str());
    }
    handle._localPort = config.GetPort();
    return handle;
}

// StockHistorySimulation* Server::CreateSimulation(const String& name, const String& strategy, int type) {
//     auto ptr = new StockHistorySimulation(this);
//     auto info = GetExchangeInfo(name.c_str());
//     if (!ptr->Init(info)) {
//         printf("init fail.\n");
//         delete ptr;
//         return nullptr;
//     }
//     if (!ptr->Login(AccountType::MAIN)) {
//         printf("login fail.\n");
//         return nullptr;
//     }
//     return ptr;
// }
