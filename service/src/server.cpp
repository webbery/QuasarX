#include "server.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <thread>
#include "Bridge/exchange.h"
#include "Bridge/SIM/ETFHistorySimulation.h"
#include "BrokerSubSystem.h"
#include "ExchangeManager.h"
#include "Handler/PositionHandler.h"
#include "Handler/RiskHandler.h"
#include "Nodes/DebugNode.h"
#include "Nodes/FormulaNode.h"
#include "Nodes/FunctionNode.h"
#include "RiskSubSystem.h"
#include "Handler/ServerEventHandler.h"
#include "Handler/RecordHandler.h"
#include "Handler/ReplayHandler.h"
#include "HttpHandler.h"
#include "StrategyNode.h"
#include "Data/QuoteData.h"
#include "Data/MarginData.h"
#include "Data/FundamentalData.h"
#include "Util/QuoteDB.h"
#include "Util/DecisionDB.h"
#include "Util/DuckDBLogger.h"
#include "Util/HolidayCalendar.h"
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
#include "Handler/CointegrationHandler.h"
#include "Handler/SignalHandler.h"
#include "Handler/NonlinearHandler.h"
#include "Handler/PCAHandler.h"
#include "Handler/RegressionHandler.h"
#include "Handler/CUSUMHandler.h"
#include "Handler/OrderHandler.h"
#include "Handler/NavHandler.h"
#include "Handler/StrategyHandler.h"
#include "Handler/StrategyPerformanceHandler.h"
#include "Handler/StrategyTradeHandler.h"
#include "Handler/StockHandler.h"
#include "Handler/StrategyLogHandler.h"
#include "Handler/PythonRunnerHandler.h"
#include "Handler/MLHandler.h"
#include "Handler/HMMHandler.h"
#include "Handler/StabilityHandler.h"
#include "Handler/QuoteDownloadHandler.h"
#include "Util/PythonRunner.h"
#include "Handler/QuoteDataHandler.h"
#include "Handler/FinanceHandler.h"
#include "Handler/FinanceDataHandler.h"
#include "Handler/DividendHandler.h"
#include "Handler/MarginHandler.h"
#include "Nodes/QuoteNode.h"
#include "Nodes/SignalNode.h"
#include "Nodes/PortfolioNode.h"
#include "Nodes/ExecuteNode.h"
#include "Handler/BrokerHandler.h"
#include "Handler/RiskHandler.h"
#include "Handler/PortfolioHandler.h"
#include "Handler/FutureHandler.h"
#include "Handler/OptionHandler.h"
#include "Handler/OptionDataHandler.h"
#include "Handler/OptionPricingHandler.h"
#include "Handler/IndexHandler.h"
#include "Handler/BackTestHandler.h"
#include "Handler/CapacityHandler.h"
#include "Handler/RiskStatusHandler.h"
#include "Handler/SRHandler.h"
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
#include "Handler/HMMHandler.h"
#include "Util/ThreadPool.h"
#if defined(_DEBUG) || defined(ENABLE_SIMULATE_BAR)
#include "Handler/SimulateBarHandler.h"
#endif
#include "StrategySubSystem.h"
#include "AgentSubSystem.h"
#include "Util/FinanceDB.h"
#include "Util/DailyDecision.h"
#include "Util/data.h"
#include "nng/nng.h"
#include "jwt-cpp/traits/nlohmann-json/traits.h"
#include <boost/algorithm/string.hpp>

// 定义在文件末尾（与邮件发送共用）
static String ShellQuote(const String& s);

#define THREAD_URL  "inproc://thread"
#define ERROR_RESPONSE  "not a valid request"

#define HISTORY_FILENAME  ".cmd_hist"

#define RegistHandler(name, type) \
    { type* recorder = new type(this);\
        _handlers[name] = recorder; }

// 路由分发：handler 抛出的异常必须落进 spdlog。
// httplib 默认只把异常写进 stderr（不进 spdlog 文件），CI 抓的服务日志里看不到，
// 表现为「路由命中但零日志 + 非 200 响应」，无法定位是哪个脚本。
// 这里记完日志再原样重抛 —— httplib 后续的 500 + EXCEPTION_WHAT 行为完全不变。
#define DISPATCH_HANDLER(call) \
    try {\
        call;\
    } catch (const std::exception &e) {\
        WARN("[HTTP] handler 抛出异常: {} {}", req.path, e.what());\
        throw;\
    } catch (...) {\
        WARN("[HTTP] handler 抛出未知异常: {}", req.path);\
        throw;\
    }

#define REGIST_GET(api_name) \
_svr.Get(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Get " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    DISPATCH_HANDLER(this->_handlers[api_name]->get(req, res))\
})
#define REGIST_PUT(api_name) \
_svr.Put(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Put " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    DISPATCH_HANDLER(this->_handlers[api_name]->put(req, res))\
})
#define REGIST_POST(api_name) \
_svr.Post(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Post " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    DISPATCH_HANDLER(this->_handlers[api_name]->post(req, res))\
})
#define REGIST_DEL(api_name) \
_svr.Delete(API_VERSION api_name, [this](const httplib::Request & req, httplib::Response &res) {\
    INFO("Del " API_VERSION api_name);\
    if (!JWTMiddleWare(req, res)) {\
        return;\
    }\
    res.set_header("Access-Control-Expose-Headers", "EXCEPTION_WHAT");\
    DISPATCH_HANDLER(this->_handlers[api_name]->del(req, res))\
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
#define API_OPTION_DATA     "/option/data"
#define API_OPTION_PRICING  "/option/pricing"
#define API_OPTION_PRICING_MULTI "/option/pricing_multi"
#define API_OPTION_IV_SURFACE    "/option/iv_surface"
#define API_STOCK_DETAIL    "/stocks/detail"
#define API_STOCK_HISTORY   "/stocks/history"
#define API_ALL_STOCK       "/stocks/simple"
#define API_STOCK_PRIVILEGE "/stocks/privilege"
#define API_STOCK_PARAMS    "/stocks/params"
#define API_EXHANGE         "/exchange"
#define API_PORTFOLIO       "/portfolio"
#define API_COMMISSION      "/commission"
#define API_STRATEGY        "/strategy"
#define API_STRATEGY_PERFORMANCE "/strategy/performance"
#define API_STRATEGY_TRADE  "/strategy/trade"
#define API_STRATEGY_NODES  "/strategy/nodes"
#define API_STRATEGY_NODE   "/strategy/node"
#define API_BACKTEST        "/backtest"
#define API_CAPACITY        "/capacity"
#define API_RISK_STATUS     "/risk/status"
#define API_RISK_RESET      "/risk/reset-breaker"
#define API_INDEX           "/index/quote"
#define API_MONTECARLO      "/predict/montecarlo"
#define API_FINITE_DIFF     "/predict/finite_diff"
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
#define API_TRADE_DECISIONS "/trade/decisions"
#define API_POSITION        "/position"
#define API_SERVER_EVENT    "/server/event"
#define API_RISK_CAPITAL    "/risk/capital"
#define API_RISK_DAILY      "/risk/daily"
#define API_RISK_CLOSEALL   "/risk/closeall"
#define API_RISK_STRATEGIES "/risk/strategies"
#define API_NAV_HISTORY     "/nav/history"
#define API_VOLATILITY      "/analysis/volatility"
#define API_COINTEGRATION   "/analysis/cointegration"
#define API_COVARIANCE_DIAG "/analysis/covariance_diagnostics"
#define API_CUSUM           "/analysis/cusum"
#define API_SIGNAL          "/analysis/signal"
#define API_NONLINEAR       "/analysis/nonlinear"
#if defined(_DEBUG) || defined(ENABLE_SIMULATE_BAR)
#define API_SIMULATE_BAR    "/strategy/simulate/bar"
#endif
#define API_PCA             "/analysis/pca"
#define API_ANALYSIS_REGRESSION "/analysis/regression"
#define API_SR              "/sr/run"
#define API_CAPITAL_STATUS  "/server/capital"
#define API_STRATEGY_LOGS   "/strategy/logs"
#define API_PYTHON_RUNNER   "/python/run"
#define API_QUOTE           "/quote"
#define API_QUOTE_DATA      "/quote/data"
#define API_FINANCE         "/finance"
#define API_FINANCE_DATA    "/finance/data"
#define API_DIVIDEND        "/dividend"
#define API_MARGIN          "/margin"
#define API_ML              "/ml"
#define API_HMM             "/hmm"
#define API_STABILITY       "/analysis/stability"

void trim(std::string& input) {
  if (input.empty()) return ;

  input.erase(0, input.find_first_not_of(" "));
  input.erase(input.find_last_not_of(" ") + 1);
}

std::multimap<std::string, ContractInfo> Server::_markets;
std::map<time_t, float> Server::_inter_rates;
bool Server::_exit = false;

Server::Server():_config(nullptr),
_strategySystem(nullptr), _brokerSystem(nullptr),
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
    // 先让数据线程池收尾：刷新任务会写 DuckDB，必须在库关闭之前结束
    if (_dataPool) {
        _dataPool->shutdown();
        _dataPool.reset();
    }
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

    // 首次启动加载中国交易日历：先读本地缓存，再拉当前年份
    // 网络失败时保留缓存数据，WARN 不 FATAL（期权行权日退化为纯周末顺延）
    {
        String cacheDir = HolidayCalendar::defaultCacheDir(_config->GetDatabasePath());
        HolidayCalendar::instance().loadFromCache(cacheDir);
        std::tm lt{};
        time_t now = Now();
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        HolidayCalendar::instance().ensureCurrentYearLoaded(1900 + lt.tm_year);
    }

    // _mode = mode;
    return true;
}

void Server::Run() {
    Regist();
    auto port = _config->GetPort();

    // 覆盖 httplib 默认 ThreadPool 线程数: 总硬件并发的一半
    // (避免与回测/CPU 任务抢资源, 最小 2 线程兜底)
    {
        unsigned int hw = std::thread::hardware_concurrency();
        if (hw == 0) hw = 8;
        unsigned int half = hw / 2;
        if (half < 2) half = 2;
        _svr.new_task_queue = [half]() -> httplib::TaskQueue* {
            return new httplib::ThreadPool(half);
        };
        INFO("httplib ThreadPool: {} workers (hw={} / 2)", half, hw);
    }

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

void Server::Stop() {
    _svr.stop();
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

#if defined(_DEBUG) || defined(ENABLE_SIMULATE_BAR)
    REGIST_POST(API_SIMULATE_BAR);
#endif

    // /risk/var 与 /predict/montecarlo 路由已禁用：缺少对应 Handler 实现，
    // 注册会导致 _handlers[...] 返回 nullptr 并在调用时 segfault。
    // 待 VaRHandler / MonteCarloHandler 实现后重新启用。

    REGIST_GET(API_ALL_STOCK);

    REGIST_POST(API_EXHANGE);

    REGIST_POST(API_RECORD);
    REGIST_DEL(API_RECORD);

    REGIST_POST(API_REPLAY);

    REGIST_GET(API_STOCK_DETAIL);

    REGIST_PUT(API_PORTFOLIO);

    REGIST_GET(API_STRATEGY);
    REGIST_GET(API_STRATEGY_PERFORMANCE);
    REGIST_GET(API_STRATEGY_TRADE);
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
    REGIST_GET(API_TRADE_DECISIONS);
    REGIST_PUT(API_TRADE_DECISIONS);
    REGIST_DEL(API_TRADE_DECISIONS);
    REGIST_GET(API_USER_FUNDS);
    REGIST_GET(API_POSITION);
    REGIST_GET(API_STRATEGY_NODES);
    REGIST_GET(API_STRATEGY_NODE);
    REGIST_GET(API_STRATEGY_LOGS);
    REGIST_DEL(API_STRATEGY_LOGS);
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
    REGIST_POST(API_MARGIN);
    REGIST_GET(API_MARGIN);
    REGIST_DEL(API_MARGIN);
    REGIST_POST(API_OPTION_DATA);
    REGIST_GET(API_OPTION_DATA);
    REGIST_DEL(API_OPTION_DATA);
    REGIST_POST(API_OPTION_PRICING);
    REGIST_POST(API_OPTION_PRICING_MULTI);
    REGIST_GET(API_OPTION_IV_SURFACE);
    REGIST_POST(API_ML);
    REGIST_GET(API_ML);
    REGIST_DEL(API_ML);
    REGIST_POST(API_HMM);
    REGIST_GET(API_HMM);
    REGIST_DEL(API_HMM);
    REGIST_POST(API_STABILITY);
    REGIST_GET(API_STABILITY);
    REGIST_GET(API_STOCK_PRIVILEGE);
    REGIST_GET(API_STOCK_PARAMS);
    REGIST_GET(API_OPTION_HISTORY);
    REGIST_GET(API_CAPITAL_STATUS);

    REGIST_POST(API_BACKTEST);
    REGIST_POST(API_CAPACITY);
    REGIST_GET(API_RISK_STATUS);
    REGIST_POST(API_RISK_RESET);
    REGIST_GET(API_VOLATILITY);
    REGIST_GET(API_COINTEGRATION);
    REGIST_POST(API_COVARIANCE_DIAG);
    REGIST_POST(API_CUSUM);
    REGIST_GET(API_SIGNAL);
    REGIST_GET(API_NONLINEAR);
    REGIST_GET(API_PCA);
    REGIST_GET(API_ANALYSIS_REGRESSION);
    REGIST_POST(API_ANALYSIS_REGRESSION);
    REGIST_POST(API_SR);
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
        if (quoteDB.init(db_path + "/quote", "quote.db")) {
            INFO("[InitDefault] QuoteDB initialized at {}/quote", db_path);
        } else {
            WARN("[InitDefault] Failed to initialize QuoteDB at {}/quote", db_path);
        }
    }

    // 初始化 DecisionDB
    auto& decisionDB = DecisionDB::instance();
    if (!decisionDB.isInitialized()) {
        if (decisionDB.init(db_path + "/decisions", "decisions.db")) {
            INFO("[InitDefault] DecisionDB initialized at {}/decisions", db_path);
        } else {
            WARN("[InitDefault] Failed to initialize DecisionDB at {}/decisions", db_path);
        }
    }

    // 数据刷新状态（记录每条链路刷到哪个交易日，进程重启后仍能判断要不要补）
    _refreshState = std::make_unique<RefreshState>(db_path);

    // 数据线程池：worker 数必须压住。
    // 远程机器只有 2 核，主线程池已经占满 hardware_concurrency 个 worker，
    // 这里再多开就是几个 python3 抢 CPU。≤4 核固定 1 个（三条链路串行排队），
    // 更多核才给 2 个。历史上 50+ 并发 python（每个 25~30MB）把 1.6G 的机器
    // 直接 OOM 掉过，宁可慢也不能再爆。
    unsigned hc = std::thread::hardware_concurrency();
    size_t dataWorkers = (hc >= 8) ? 2 : 1;
    _dataPool = std::make_unique<ThreadPool>(dataWorkers);
    INFO("[InitDefault] 数据刷新线程池: {} worker(s), 硬件线程 {}", dataWorkers, hc);

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

    _brokerSystem = new BrokerSubSystem(this, use_sim);
    String dbpath = broker["db"];
    if (!std::filesystem::exists(dbpath)) {
        std::filesystem::create_directories(dbpath);
    }

    auto& exchagnes = _exchangeMgr->GetExchangesByType();

    _brokerSystem->Init(broker, exchagnes);
    
    // 初始化账本
    double initialCapital = 1000000;  // 默认 100 万
    if (default_config.contains("capitalRisk") && default_config["capitalRisk"].contains("initialCapital")) {
        initialCapital = default_config["capitalRisk"]["initialCapital"].get<double>();
    }

    // TODO(券商接入)：按 broker["api"] 创建 IBrokerLedger 实现并注册。
    // 未注册时使用本地账本。
    // _brokerSystem->SetBrokerLedger(new HXBrokerLedger(...));

    _brokerSystem->initLedger(initialCapital, dbpath + "/" + LEDGER_FILE_NAME);

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
        // 按 SignalNodeBase 判定而非 SignalNode：PairNode 等信号类节点直接继承
        // SignalNodeBase，若按 SignalNode 判定，配对交易图会被误判为
        // "Signal(信号节点) is lost"。
        int inputCount = 0, signalCount = 0, portfolioCount = 0, executionCount = 0;
        for (auto* node : nodes) {
            if (dynamic_cast<QuoteInputNode*>(node)) inputCount++;
            else if (dynamic_cast<SignalNodeBase*>(node)) signalCount++;
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
            // 收集孤立节点的详细信息
            List<String> isolatedNodeInfos;
            for (auto* node : nodes) {
                if (!visited.count(node)) {
                    String nodeType = "unknown";
                    if (dynamic_cast<QuoteInputNode*>(node)) nodeType = "input";
                    else if (dynamic_cast<SignalNodeBase*>(node)) nodeType = "signal";
                    else if (dynamic_cast<PortfolioNode*>(node)) nodeType = "portfolio";
                    else if (dynamic_cast<ExecuteNode*>(node)) nodeType = "execution";
                    else if (dynamic_cast<FunctionNode*>(node)) nodeType = "function";
                    else if (dynamic_cast<FormulaNode*>(node)) nodeType = "formula";
                    else if (dynamic_cast<ProtectionNode*>(node)) nodeType = "protection";
                    else if (dynamic_cast<DebugNode*>(node)) nodeType = "debug";
                    
                    String nodeInfo = std::format("ID={} (type={})", node->id(), nodeType);
                    isolatedNodeInfos.push_back(nodeInfo);
                }
            }
            
            lambda_delete(nodes);
            String errorMsg = std::format("策略图存在孤立的节点，总节点数={}，可到达节点数={}，孤立节点: [{}]",
                nodes.size(), visited.size(), boost::algorithm::join(isolatedNodeInfos, "; "));
            WARN("{}", errorMsg);
            return {false, errorMsg};
        }

        // 4. 检查 Input 是否能到达 Signal/Portfolio/Execution
        bool canReachSignal = false, canReachPortfolio = false, canReachExecution = false;
        for (auto* node : visited) {
            if (dynamic_cast<SignalNodeBase*>(node)) canReachSignal = true;
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
        position["price"] = fmt::format("{:.4f}", item._avgPrice);
        position["curPrice"] = fmt::format("{:.4f}", item._currentPrice);
        position["name"] = to_utf8(item._name);
        position["quantity"] = std::to_string(item._quantity);
        position["valid_quantity"] = std::to_string(item._validQuantity);
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
    // static bool daily_force_done = false;
    static bool yearly_refresh_done = false;
    if (prev_day == -1) {
        prev_day = ltm->tm_wday;
    }
    if (prev_day != ltm->tm_wday) {
        daily_once = false;
        daily_init_done = false;
        // daily_force_done = false;
        yearly_refresh_done = false;
        prev_day = ltm->tm_wday;
    }

    // 12 月 20 日 年度刷新：拉取下一年的中国假日数据（异步，不阻塞 5s 定时循环）
    // 国务院通常 11 月底发布次年放假安排，12 月 20 日留 ~10 天缓冲以应对官方临时调整
    if (!yearly_refresh_done
        && ltm->tm_mon == 11       // tm_mon 0-based: 11 = 12 月
        && ltm->tm_mday == 20
    ) {
        yearly_refresh_done = true;
        int nextYear = 1900 + ltm->tm_year + 1;
        std::thread([nextYear]() {
            bool ok = HolidayCalendar::instance().ensureYearLoaded(nextYear);
            if (!ok) {
                nng_socket sseSock = Server::GetSocket();
                if (sseSock.id != 0) {
                    SendSSE(sseSock, "schedule_error", {
                        {"task", "holiday_calendar_refresh"},
                        {"year", std::to_string(nextYear)},
                        {"message", "fetch failed; keeping in-memory data"}
                    });
                }
            }
        }).detach();
    }

    // 15:00 初始化日级策略执行（收盘数据写入后由 TickFlowBridge 触发 MarkSymbolReady）
    if (!daily_init_done
#ifndef _DEBUG
        && ltm->tm_hour == 15 && ltm->tm_min == 0
#endif
    ) {
        daily_init_done = true;
        // 节假日检查：节假日不执行日终策略
        int year = 1900 + ltm->tm_year;
        int month = 1 + ltm->tm_mon;
        int day = ltm->tm_mday;
        bool isHoliday = HolidayCalendar::instance().isHoliday(year, month, day);
        if (isHoliday) {
            INFO("[Schedules] Today is holiday ({}-{:02d}-{:02d}), skip daily strategy execution",
                 year, month, day);
        } else if (_strategySystem) {
            _strategySystem->EnsureDailyReady();
            INFO("[Schedules] Daily execution initialized at 15:00");
        }
    }

    // 15:30 超时兜底：强制执行所有未完成的策略
    // if (!daily_force_done && ltm->tm_hour == 15 && ltm->tm_min == 30) {
    //     daily_force_done = true;
    //     if (_strategySystem) {
    //         _strategySystem->ForceExecuteAllDaily();
    //         INFO("[Schedules] Force executed all daily strategies at 15:30");
    //     }
    // }

    // ── 日终任务（到点或过点触发）──
    //
    // 触发条件从「正好落在 20:00 这一分钟」改成「到了或过了这个点」：
    // 5 秒一个 tick，进程在 20:00 那一分钟重启、或者 23:00 才起来，
    // 以前那一整天的任务就直接丢了。daily_once 在午夜重置，不会重复执行。
    auto dailyTime = _config->GetDailyTime();
    bool pastDaily = (ltm->tm_hour > dailyTime.first) ||
                     (ltm->tm_hour == dailyTime.first && ltm->tm_min >= dailyTime.second);
    if (!daily_once && pastDaily) {
        daily_once = true;
        LOG("run once script");
        nng_socket sseSock = GetSocket();

        // 数据刷新（行情 / 两融 / 基本面）：判据是「目标交易日刷过没有」，
        // 所以晚启动、昨天失败、20:00 那分钟重启过都会自动补上。
        RefreshDailyData();

        // 更新分红除权数据（每周日）：分红数据是历史事实，
        //    公告后基本不变，避免每日全量 upsert 触发 DuckDB ART 索引崩溃
        if (ltm->tm_wday == 0 && _strategySystem) {
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
                INFO("[Schedules] Sunday dividend update: {} symbols from {} strategies",
                     allSymbols.size(), names.size());

                // 下载目录必须和导入目录是同一个绝对路径。
                // 以前写的是 --data-dir data 配 cd ../tools，脚本落在
                // service/tools/data/dividend/，导入却读 {dbPath}/dividend ——
                // 结果每周都在导入一批陈年 CSV。
                String dbPath = _config->GetDatabasePath();
                String dividendDir =
                    std::filesystem::absolute(dbPath + "/dividend").string();
                std::error_code mk_ec;
                std::filesystem::create_directories(dividendDir, mk_ec);

                String interpreter =
                    PythonEnv::fromConfig(_config->GetRawConfig()).resolve("");
                String cmd = "cd ../tools && " + interpreter +
                             " fetch_dividend_data.py " + ShellQuote(symbolsStr) +
                             " --download --data-dir " + ShellQuote(dividendDir);
                String output;
                bool ok = RunCommand(cmd, output);

                // 下载失败就不导入。此前"无论下载是否成功都尝试导入已有文件"，
                // 会把目录里遗留的旧 CSV（还包含池外标的）写进 dividend 表，
                // 随后的复权重算按这些错误事件算因子，原始价被放大成
                // 累计复权倍数，行情特征随之失真。
                int imported = 0;
                int failedFiles = 0;
                if (!ok) {
                    WARN("[Schedules] fetch_dividend_data.py 执行失败，跳过分红导入与复权重算");
                } else {
                    // 只导入名单内的标的：目录里可能躺着别的任务留下的 CSV
                    imported = FinanceDB::instance().importAllDividends(
                        dividendDir, allSymbols, &failedFiles);
                    if (failedFiles > 0) {
                        WARN("[Schedules] 分红导入有 {} 个文件失败，事件表不完整", failedFiles);
                    }
                }

                if (!ok) {
                    String msg = "fetch_dividend_data.py 执行失败，已跳过分红导入与复权重算";
                    WARN("[Schedules] {}", msg);
                    SendSSE(sseSock, "schedule_error", {
                        {"task", "fetch_dividend_data.py"},
                        {"message", msg},
                        {"symbols", symbolsStr},
                        {"imported_rows", std::to_string(imported)},
                        {"output", output}
                    });
                } else if (imported == 0) {
                    String msg = fmt::format("分红数据下载成功但未导入任何记录（{} 个标的）",
                                             allSymbols.size());
                    WARN("[Schedules] {}", msg);
                    SendSSE(sseSock, "schedule_error", {
                        {"task", "fetch_dividend_data.py"},
                        {"message", msg},
                        {"symbols", symbolsStr},
                        {"imported_rows", "0"}
                    });
                }
            }
        }

        // 融资融券标的（可做空标的）名单：每周六晚间刷新一次。
        // 名单是慢变量（交易所按月调整），按周而非按日，避免每次都付几分钟抓取代价。
        if (ltm->tm_wday == 6) {  // tm_wday 0-based: 6 = 周六
            RefreshMarginStockList(sseSock);
        }

        if (prev_day == 6) {
            // every week 6 run once
            String output;
            String interpreter =
                PythonEnv::fromConfig(_config->GetRawConfig()).resolve("");
            bool ok = RunCommand("cd ../tools && " + interpreter + " run_task.py 3", output);
            if (!ok) {
                String msg = "run_task.py 3 执行失败";
                WARN("[Schedules] {}", msg);
                SendSSE(sseSock, "schedule_error", {
                    {"task", "run_task.py"},
                    {"message", msg},
                    {"output", output}
                });
            }
            ReloadMarketData(_config->GetDatabasePath());
        }

        // 每日清理过期数据，防止 DuckDB 无限膨胀
        int64_t tick_cutoff = static_cast<int64_t>(t) - 30 * 86400;
        int64_t tick_deleted = DuckDBLogger::instance().delete_tick_data_before(tick_cutoff);
        if (tick_deleted > 0) {
            INFO("[Schedules] Cleaned {} tick records older than 30 days", tick_deleted);
        }
        DuckDBLogger::instance().cleanup_old_logs(90);
    }

    // 03:00 之后补数据：把上一轮体检遗留的异常重新下载
    // 同样用「到点或过点」而不是「正好 03:00」，进程晚启动也能补。
    if (ltm->tm_hour >= 3) {
        RepairDailyData();
    }
}

void Server::RefreshMarginStockList(nng_socket sseSock) {
    auto& financeDB = FinanceDB::instance();

    // FinanceDB 是懒初始化的（各消费方自己 init），而本函数可能先于任何
    // 回测/行情流程触发，必须自己确保打开过
    if (!financeDB.isInitialized() &&
        !financeDB.init(_config->GetDatabasePath() + "/finance", "finance.db")) {
        WARN("[RefreshMarginStock] FinanceDB init failed，原名单保持不变");
        SendSSE(sseSock, "schedule_error", {
            {"task", "fetch_shortable_stocks.py"},
            {"message", "FinanceDB init failed; 原名单保持不变"}
        });
        return;
    }

    INFO("[RefreshMarginStock] 拉取融资融券标的名单...");
    String output;
    bool ok = RunCommand("cd ../tools && " + std::string(PYTHON_CMD) +
                         " fetch_shortable_stocks.py", output);

    int imported = 0;
    if (ok && !output.empty()) {
        // 脚本 stdout 可能带非 JSON 前缀（如 Windows code page 提示），从第一个 '{' 起截
        auto json_start = output.find('{');
        try {
            auto data = nlohmann::json::parse(
                json_start != std::string::npos ? output.substr(json_start) : output);
            // importMarginStocks 对空输入返回 0 且不清表，所以失败不会毁掉现有名单
            imported = financeDB.importMarginStocks(data);
        } catch (const std::exception& e) {
            WARN("[RefreshMarginStock] 解析脚本输出失败: {}", e.what());
        }
    }

    if (!ok || imported == 0) {
        String msg = fmt::format("融资融券标的刷新失败（保留原名单），ok={} imported={}",
                                 ok, imported);
        WARN("[RefreshMarginStock] {}", msg);
        SendSSE(sseSock, "schedule_error", {
            {"task", "fetch_shortable_stocks.py"},
            {"message", msg},
            {"output", output.substr(0, 500)}
        });
        return;
    }

    INFO("[RefreshMarginStock] 已刷新 {} 只可做空标的", imported);
}

// 定义在文件下方（与邮件发送共用）
// ═══════════════════════════════════════════════════════════
//  数据刷新：行情 / 两融 / 基本面
//
//  三条链路都走 Data/ 下的公共骨架，链路内部顺序固定：
//    下载 → CSV 级体检 → 分块入库 → DB 级复查 → 补数据
//    Data/QuoteData        行情
//    Data/MarginData       两融
//    Data/FundamentalData  基本面
//
//  调度判据不是「到了 20:00 这一分钟」，而是「目标交易日还没刷过」：
//  昨天失败、进程晚启动、20:00 那分钟重启过，都会在下一个 tick 自动补上。
// ═══════════════════════════════════════════════════════════

namespace {

// 数据线程池里的任务自己开 socket，不走 GetSocket()。
// GetSocket() 是 thread_local 且被 ScopedSocket 关掉后不会把 id 归零，
// 同一 worker 上跑第二个任务就会拿到一个已关闭的 socket。
ScopedSocket OpenSseSocket() {
    ScopedSocket sock;
    Pusher(URI_SERVER_EVENT, sock.get());
    return sock;
}

}  // namespace

Vector<symbol_t> Server::CollectStrategySymbols() {
    Vector<symbol_t> result;
    if (!_strategySystem) return result;

    Set<symbol_t> seen;
    for (auto& name : _strategySystem->GetStrategyNames()) {
        try {
            for (auto sym : _strategySystem->GetPools(name)) {
                if (seen.insert(sym).second) {
                    result.push_back(sym);
                }
            }
        } catch (...) {
            WARN("[DataRefresh] GetPools({}) failed", name);
        }
    }
    return result;
}

bool Server::DispatchDataJob(const String& key, std::function<void()> job) {
    if (!_dataPool) return false;
    {
        std::lock_guard<std::mutex> lock(_dataInflightMtx);
        if (_dataInflight.count(key) > 0) {
            return false;   // 已经在跑，别重复派发
        }
        _dataInflight.insert(key);
    }

    _dataPool->submit([this, key, job = std::move(job)]() {
        try {
            job();
        } catch (const std::exception& e) {
            WARN("[DataRefresh] {} 异常: {}", key, e.what());
        } catch (...) {
            WARN("[DataRefresh] {} 未知异常", key);
        }
        std::lock_guard<std::mutex> lock(_dataInflightMtx);
        _dataInflight.erase(key);
    });
    return true;
}

void Server::SubmitDataJob(
        const String& key, const String& event,
        Map<String, String> started,
        std::function<Map<String, String>(const DataJobResult&)> finished,
        std::function<DataJobResult()> run) {
    auto job = [this, key, event,
                started = std::move(started),
                finished = std::move(finished),
                run = std::move(run)]() {
        // 数据线程池的 worker 是共享的，socket 必须在本线程创建并随任务关闭
        ScopedSocket sse = OpenSseSocket();
        if (!started.empty()) {
            SendSSE(sse.get(), event, started);
        }

        DataJobResult result;
        try {
            result = run();
        } catch (const std::exception& e) {
            result.rows = -1;
            result.error = e.what();
            WARN("[DataJob] {} 异常: {}", key, e.what());
        } catch (...) {
            result.rows = -1;
            result.error = "unknown exception";
            WARN("[DataJob] {} 未知异常", key);
        }

        if (finished) {
            SendSSE(sse.get(), event, finished(result));
        }
    };

    if (!DispatchDataJob(key, std::move(job))) {
        WARN("[DataJob] {} 已在运行，忽略重复提交", key);
    }
}

int Server::DownloadQuoteData(const Vector<symbol_t>& symbols, const String& freq,
                              bool overwrite, const String& env) {
    String dbPath = _config->GetDatabasePath();
    String interpreter = PythonEnv::fromConfig(_config->GetRawConfig()).resolve(env);

    QuoteData data(interpreter, freq);
    data.SetSymbols(symbols);
    data.SetOverwrite(overwrite);

    DataPipeline pipeline(data);
    return pipeline.Run(dbPath + "/quote");
}

void Server::SubmitQuoteDownload(const Vector<symbol_t>& symbols, const String& freq,
                                 bool overwrite, const String& env) {
    SubmitDataJob(
        "download:" + freq, "quote_download",
        {
            {"status", "started"},
            {"freq", freq},
            {"total", std::to_string(symbols.size())}
        },
        [freq](const DataJobResult& r) -> Map<String, String> {
            const bool ok = r.rows >= 0;
            return {
                {"status", ok ? "done" : "failed"},
                {"freq", freq},
                {"total_rows", std::to_string(r.rows)},
                {"success", ok ? "true" : "false"}
            };
        },
        [this, symbols, freq, overwrite, env]() -> DataJobResult {
            return {DownloadQuoteData(symbols, freq, overwrite, env), 0, {}};
        });
}

void Server::SubmitMarginDownload(const String& date, const String& env) {
    const String shownDate = date.empty() ? "latest" : date;
    SubmitDataJob(
        "margin:" + shownDate, "margin_download",
        {
            {"status", "started"},
            {"date", shownDate}
        },
        [](const DataJobResult& r) -> Map<String, String> {
            if (r.rows < 0) {
                return {{"status", "failed"}, {"error", r.error}};
            }
            return {{"status", "completed"}, {"rows", std::to_string(r.rows)}};
        },
        [this, date, env]() -> DataJobResult {
            String error;
            int n = DownloadMarginData(date, env, error);
            return {n, 0, error};
        });
}

void Server::SubmitFundamentalDownload(const Vector<symbol_t>& symbols,
                                       const Vector<DataCategory>& categories,
                                       const String& start, const String& end,
                                       const String& env) {
    SubmitDataJob(
        "fundamental", "fundamental_refresh",
        {},   // 基本面任务本来就不发 started 事件
        [](const DataJobResult& r) -> Map<String, String> {
            return {
                {"status", r.rows >= 0 ? "completed" : "error"},
                {"imported", std::to_string(r.rows)},
                {"issues", std::to_string(r.issues)}
            };
        },
        [this, symbols, categories, start, end, env]() -> DataJobResult {
            String dbPath = _config->GetDatabasePath();
            String interpreter = PythonEnv::fromConfig(_config->GetRawConfig()).resolve(env);

            FundamentalData data(dbPath + "/finance", interpreter);
            data.SetSymbols(symbols);
            if (!categories.empty()) {
                data.SetCategories(categories);
            }
            data.SetWindow(start, end);

            DataPipeline pipeline(data);
            int n = pipeline.Run(dbPath + "/finance");
            return {n, pipeline.issues().size(), {}};
        });
}

int Server::DownloadMarginData(const String& date, const String& env, String& error) {
    String dbPath = _config->GetDatabasePath();
    String interpreter = PythonEnv::fromConfig(_config->GetRawConfig()).resolve(env);

    MarginData data(dbPath + "/finance", interpreter);
    if (!date.empty()) {
        data.SetTargetDay(date);
    }

    DataPipeline pipeline(data);
    int n = pipeline.Run(dbPath + "/margin");
    if (n < 0) {
        error = "两融明细抓取或入库失败";
    }
    return n;
}

void Server::RunQuoteRefresh(const String& targetDay) {
    String dbPath = _config->GetDatabasePath();
    String interpreter = PythonEnv::fromConfig(_config->GetRawConfig()).resolve("");
    String quoteDir = dbPath + "/quote";

    Vector<symbol_t> symbols = CollectStrategySymbols();
    if (symbols.empty()) {
        WARN("[DataRefresh] 没有策略池标的，跳过行情刷新");
        return;
    }

    ScopedSocket sse = OpenSseSocket();

    // 逐个频率串行跑：一台机器同时拉起多个 python3 下载进程的代价
    // 远大于并行省下的那点时间（历史上有过 OOM）。
    static const char* kFreqs[] = {"daily", "5m", "15m", "30m", "60m"};
    bool allOk = true;

    for (const char* freq : kFreqs) {
        QuoteData data(interpreter, freq);
        data.SetSymbols(symbols);
        data.SetEndDate(targetDay);

        DataPipeline pipeline(data);
        int n = pipeline.Run(quoteDir);

        INFO("[DataRefresh] 行情 {} 完成: 导入 {} 行, 异常 {} 条",
             freq, n, pipeline.issues().size());

        SendSSE(sse.get(), "quote_download", {
            {"status", n >= 0 ? "completed" : "error"},
            {"freq", freq},
            {"imported", std::to_string(n)},
            {"issues", std::to_string(pipeline.issues().size())}
        });

        if (n < 0) allOk = false;
    }

    if (allOk) {
        _refreshState->MarkDone("quote", targetDay);
    } else {
        SendSSE(sse.get(), "schedule_error", {
            {"task", "download_quote_bs.py"},
            {"message", "行情刷新有频率失败，下一轮会重试"},
            {"date", targetDay}
        });
    }
}

void Server::RunMarginRefresh(const String& targetDay) {
    String dbPath = _config->GetDatabasePath();
    String interpreter = PythonEnv::fromConfig(_config->GetRawConfig()).resolve("");

    MarginData data(dbPath + "/finance", interpreter);
    data.SetTargetDay(targetDay);

    DataPipeline pipeline(data);
    int n = pipeline.Run(dbPath + "/margin");

    ScopedSocket sse = OpenSseSocket();
    if (n >= 0) {
        _refreshState->MarkDone("margin", targetDay);
        SendSSE(sse.get(), "margin_refresh", {
            {"status", "completed"},
            {"date", targetDay},
            {"imported", std::to_string(n)},
            {"issues", std::to_string(pipeline.issues().size())}
        });
    } else {
        SendSSE(sse.get(), "schedule_error", {
            {"task", "fetch_margin_data.py"},
            {"message", "两融明细刷新失败，下一轮会重试"},
            {"date", targetDay}
        });
    }
}

void Server::RunFundamentalRefresh(const String& targetDay) {
    String dbPath = _config->GetDatabasePath();
    String interpreter = PythonEnv::fromConfig(_config->GetRawConfig()).resolve("");

    Vector<symbol_t> symbols = CollectStrategySymbols();
    if (symbols.empty()) {
        WARN("[DataRefresh] 没有策略池标的，跳过基本面刷新");
        return;
    }

    // 下载窗口用「年 + 季度」，与 fetch_finance_data.py 的 --start/--end 语义对齐。
    // 以前传的是完整日期（2026-07-01），脚本靠 split('-')[0] 侥幸取到年份，
    // 而 download_category 根本不按 statDate 过滤 —— 实际是把整年四个季度全拉下来。
    time_t now = Now();
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    int year = tm.tm_year + 1900;
    int quarter = (tm.tm_mon) / 3 + 1;   // tm_mon 0-based
    String startYq = fmt::format("{}-Q{}", year, quarter);

    FundamentalData data(dbPath + "/finance", interpreter);
    data.SetSymbols(symbols);
    data.SetWindow(startYq, startYq);

    DataPipeline pipeline(data);
    int n = pipeline.Run(dbPath + "/finance");

    ScopedSocket sse = OpenSseSocket();
    if (n >= 0) {
        _refreshState->MarkDone("fundamental", targetDay);
        SendSSE(sse.get(), "fundamental_refresh", {
            {"status", "completed"},
            {"imported", std::to_string(n)},
            {"issues", std::to_string(pipeline.issues().size())}
        });
    } else {
        SendSSE(sse.get(), "schedule_error", {
            {"task", "fetch_finance_data.py"},
            {"message", "基本面刷新失败，下一轮会重试"}
        });
    }
}

void Server::RefreshDailyData() {
    if (!_refreshState || !_dataPool) return;

    const String target = DataPipeline::TargetTradingDay();

    if (_refreshState->NeedRefresh("quote", target)) {
        DispatchDataJob("quote", [this, target]() { RunQuoteRefresh(target); });
    }
    if (_refreshState->NeedRefresh("margin", target)) {
        DispatchDataJob("margin", [this, target]() { RunMarginRefresh(target); });
    }
    if (_refreshState->NeedRefresh("fundamental", target)) {
        DispatchDataJob("fundamental", [this, target]() { RunFundamentalRefresh(target); });
    }
}

void Server::RunDataRepair(const String& targetDay) {
    String dbPath = _config->GetDatabasePath();
    String interpreter = PythonEnv::fromConfig(_config->GetRawConfig()).resolve("");
    ScopedSocket sse = OpenSseSocket();

    // 行情：每个频率各补一次（异常清单里的分类别能区分频率）
    {
        static const char* kFreqs[] = {"daily", "5m", "15m", "30m", "60m"};
        for (const char* freq : kFreqs) {
            QuoteData data(interpreter, freq);
            DataPipeline pipeline(data);
            int n = pipeline.Repair(dbPath + "/quote");
            if (n > 0) {
                INFO("[DataRefresh] 行情 {} 补数据: {} 行", freq, n);
            }
        }
    }

    // 两融：整日重抓
    {
        MarginData data(dbPath + "/finance", interpreter);
        data.SetTargetDay(targetDay);
        DataPipeline pipeline(data);
        pipeline.Repair(dbPath + "/margin");
    }

    // 基本面：按 (标的, 类目) 补
    {
        FundamentalData data(dbPath + "/finance", interpreter);
        data.SetSymbols(CollectStrategySymbols());
        DataPipeline pipeline(data);
        pipeline.Repair(dbPath + "/finance");
    }

    SendSSE(sse.get(), "data_repair", {
        {"status", "completed"},
        {"date", targetDay}
    });
}

void Server::RepairDailyData() {
    if (!_refreshState || !_dataPool) return;

    const String target = DataPipeline::TargetTradingDay();
    if (!_refreshState->NeedRefresh("repair", target)) return;

    if (DispatchDataJob("repair", [this, target]() {
            RunDataRepair(target);
            _refreshState->MarkDone("repair", target);
        })) {
        INFO("[DataRefresh] 开始补数据，目标交易日 {}", target);
    }
}

ExchangeInterface* Server::GetExchange(ExchangeType type) {
    return _exchangeMgr->GetExchangeByType(type);
}

const char* DataFrequencyTypeToString(DataFrequencyType f) {
    switch (f) {
        case DataFrequencyType::Day:    return "daily";
        case DataFrequencyType::Week:   return "1w";
        case DataFrequencyType::Min1:   return "1m";
        case DataFrequencyType::Min5:   return "5m";
        case DataFrequencyType::Min15:  return "15m";
        case DataFrequencyType::Min30:  return "30m";
        case DataFrequencyType::Min60:  return "60m";
        case DataFrequencyType::Second: return "1s";
    }
    return "daily";
}

DataFrequencyType DataFrequencyTypeFromString(const char* s) {
    if (!s) return DataFrequencyType::Day;
    String str = s;
    if (str == "1d" || str == "daily") return DataFrequencyType::Day;
    if (str == "1w" || str == "week" || str == "weekly") return DataFrequencyType::Week;
    if (str == "1m") return DataFrequencyType::Min1;
    if (str == "5m") return DataFrequencyType::Min5;
    if (str == "15m") return DataFrequencyType::Min15;
    if (str == "30m") return DataFrequencyType::Min30;
    if (str == "60m") return DataFrequencyType::Min60;
    if (str == "1s") return DataFrequencyType::Second;
    return DataFrequencyType::Day;
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
    RegistHandler(API_ALL_FUTURE, FutureHandler);
    RegistHandler(API_ALL_OPTION, OptionHandler);
    RegistHandler(API_OPTION_HISTORY, OptionHistoryHandler);
    RegistHandler(API_STRATEGY, StrategyHandler);
    RegistHandler(API_STRATEGY_PERFORMANCE, StrategyPerformanceHandler);
    RegistHandler(API_STRATEGY_TRADE, StrategyTradeHandler);
    RegistHandler(API_STRATEGY_NODES, StrategyNodesHandler);
    RegistHandler(API_STRATEGY_NODE, StrategyNodeHandler);
    RegistHandler(API_INDEX, IndexHandler);
    RegistHandler(API_BACKTEST, BackTestHandler);
    RegistHandler(API_CAPACITY, CapacityHandler);
    RegistHandler(API_RISK_STATUS, RiskStatusHandler);
    RegistHandler(API_RISK_RESET, RiskStatusHandler);
    RegistHandler(API_TRADE_ORDER, OrderHandler);
    RegistHandler(API_TRADE_HISTORY, HistoryTradeHandler);
    RegistHandler(API_TRADE_DECISIONS, DecisionHandler);
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
    RegistHandler(API_COINTEGRATION, CointegrationHandler);
    RegistHandler(API_COVARIANCE_DIAG, VolatilityHandler);
    RegistHandler(API_CUSUM, CUSUMHandler);
    RegistHandler(API_SIGNAL, SignalHandler);
    RegistHandler(API_NONLINEAR, NonlinearHandler);
    RegistHandler(API_PCA, PCAHandler);
    RegistHandler(API_ANALYSIS_REGRESSION, RegressionHandler);
    RegistHandler(API_SR, SRHandler);
    RegistHandler(API_STRATEGY_LOGS, StrategyLogHandler);
    RegistHandler(API_PYTHON_RUNNER, PythonRunnerHandler);
    RegistHandler(API_QUOTE, QuoteDownloadHandler);
    RegistHandler(API_QUOTE_DATA, QuoteDataHandler);
    RegistHandler(API_FINANCE, FinanceHandler);
    RegistHandler(API_FINANCE_DATA, FinanceDataHandler);
    RegistHandler(API_DIVIDEND, DividendHandler);
    RegistHandler(API_MARGIN, MarginHandler);
    RegistHandler(API_OPTION_DATA, OptionDataHandler);
    RegistHandler(API_OPTION_PRICING, OptionPricingHandler);
    RegistHandler(API_OPTION_PRICING_MULTI, OptionPricingHandler);
    RegistHandler(API_OPTION_IV_SURFACE, OptionPricingHandler);
    RegistHandler(API_ML, MLHandler);
    RegistHandler(API_HMM, HMMHandler);
    RegistHandler(API_STABILITY, StabilityHandler);

#if defined(_DEBUG) || defined(ENABLE_SIMULATE_BAR)
    RegistHandler(API_SIMULATE_BAR, SimulateBarHandler);
#endif

    //StopLossHandler* risk = (StopLossHandler*)_handlers[API_RISK_STOP_LOSS];
    //risk->doWork({});

    // 关联资金风控 Handler 与 RiskSubSystem 中的 CapitalRiskManager
    // 注意：RiskSubSystem 在 FlowSubsystem 中初始化，需要在 InitDefault 中关联
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

// 去除字符串首尾的引号（单引号或双引号）
static String StripQuotes(const String& s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') ||
            (s.front() == '\'' && s.back() == '\'')) {
            return s.substr(1, s.size() - 2);
        }
    }
    return s;
}

// Shell 转义：对 argv 做单引号转义（RunCommand 走 system()，路径可能含空格）
static String ShellQuote(const String& s) {
    String result = "'";
    for (char c : s) {
        if (c == '\'') {
            result += "'\\''";  // 结束单引号，加转义单引号，再开始单引号
        } else {
            result += c;
        }
    }
    result += "'";
    return result;
}

// 私有发送接口：写临时文件 + 拼命令 + RunCommand + 清理
bool Server::SendMailInternal(const String& body, bool isHtml,
                              const String& subject,
                              const Vector<String>& attachments) {
    String ext = isHtml ? "html" : "txt";
    String tmpPath = (std::filesystem::temp_directory_path() / ("qx_mail_body." + ext)).string();
    {
        std::ofstream ofs(tmpPath);
        if (!ofs) {
            WARN("SendMailInternal: failed to create temp file {}", tmpPath);
            return false;
        }
        ofs << body;
    }

    auto sender = _config->GetSMTPSender();
    auto pwd = _config->GetSMTPPasswd();
    if (pwd.empty() || sender.empty()) {
        std::filesystem::remove(tmpPath);
        return false;
    }

    String scriptFile("tools/mail.py");
    if (!std::filesystem::exists(scriptFile)) {
        std::filesystem::remove(tmpPath);
        return false;
    }

    // 构建命令：python mail.py sender pwd receiver body content_type subject [attach1] [attach2] ...
    // 注意：配置值可能包含引号，需要先去除再转义
    String cmd = fmt::format("{} {} {} {} {} {} {}",
                             PYTHON_CMD,
                             ShellQuote(scriptFile),
                             ShellQuote(StripQuotes(sender)),
                             ShellQuote(StripQuotes(pwd)),
                             ShellQuote(StripQuotes(_config->GetWarningAddr())),
                             ShellQuote(tmpPath),
                             ShellQuote(isHtml ? "html" : "plain"));

    // 追加 subject（非空时）
    if (!subject.empty()) {
        cmd += " " + ShellQuote(subject);
    }

    // 追加附件路径
    for (const auto& attach : attachments) {
        if (std::filesystem::exists(attach)) {
            cmd += " " + ShellQuote(attach);
        } else {
            WARN("SendMailInternal: attachment not found, skipping: {}", attach);
        }
    }


    try {
        // 这里直接调 system() 而不是 RunCommand(cmd)：RunCommand 失败时会把**整条命令行**
        // 打进日志，而命令行里带着授权码。改成自己调，失败时只打印脱敏后的命令。
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            WARN("SendMailInternal: mail command failed rc={}", rc);
        }
        std::filesystem::remove(tmpPath);
        return rc == 0;
    } catch (const std::exception& e) {
        WARN("SendMailInternal: send email fail: {}", e.what());
        std::filesystem::remove(tmpPath);
        return false;
    } catch (...) {
        WARN("SendMailInternal: send email fail, unknown reason.");
        std::filesystem::remove(tmpPath);
        return false;
    }
}

bool Server::SendEmail(const String& content) {
    return SendMailInternal(content, false, "", {});
}

bool Server::SendHtmlEmail(const String& htmlContent) {
    return SendMailInternal(htmlContent, true, "", {});
}

bool Server::SendEmail(const String& content, const String& subject) {
    return SendMailInternal(content, false, subject, {});
}

bool Server::SendHtmlEmail(const String& htmlContent, const String& subject) {
    return SendMailInternal(htmlContent, true, subject, {});
}

bool Server::SendMailWithAttachments(const String& body, bool isHtml,
                                     const Vector<String>& attachments,
                                     const String& subject) {
    return SendMailInternal(body, isHtml, subject, attachments);
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
    thread_local nng_socket sock{ 0 };
    if (sock.id == 0) {
        Pusher(URI_SERVER_EVENT, sock);
    }
    return sock;
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
