#pragma once
#include "std_header.h"
#include "DataGroup.h"
#include "Handler/RiskHandler.h"
#include "Util/system.h"
#include "json.hpp"
#include "HttpHandler.h"
#include "config.h"
#include "Bridge/exchange.h"
#include <ctime>
#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include "nng/nng.h"
#include "Handler/TimerHandler.h"
#include "StrategySubSystem.h"
#include "Util/TickMap.h"

#define HEADER_SIZE         2
#define MAX_HISTORY_SIZE    32

class Commander;
class StrategySubSystem;
class PortfolioSubSystem;
class ExchangeHandler;
class StrategyPlugin;
class IStopLoss;
class StopLossHandler;
class BrokerSubSystem;

typedef enum {
	SEND_REQ, // Sending REQ request
	RECV_REP, // Receiving REQ reply
} job_state;

enum class REST_REQUEST_TYPE: char{
    REST_GET = 0,
    REST_PUT,
    REST_POST,
    REST_DELETE,
};

enum class ContractType: char {
    AStock,
    ETF,
    LOF,
    Future,
    Option,
    EureanOption,
    BinaryOption,
    BarrierOption,
    AsianOption,
    AmericanOption,
    Index,
};

struct ContractInfo {
    ContractType _type;
    ExchangeName _exchange;
};

enum class DataFrequencyType {
  Day,
  Min5,
  Second,
};

enum class StockAdjustType {
  None,
  After,
};

enum class RuningType {
  Backtest,     // 本地回测模式
  Simualtion,   // 券商模拟盘
  Real,         // 实盘
};

class Position;
class Broker;
class Server {
public:
    // 获取代码对应的市场
    static ExchangeName GetExchange(const std::string& symbol);
    static ContractType GetContractType(const std::string& symbol, const String& exhange = "");

    // enum EXECUTE_MODE: unsigned short {
    //     MODE_SERVICE,   // 网络服务模式
    //     MODE_COMMAND,   // 本地命令交互模式
    // };
    Server();
    ~Server();

    bool Init(const char* config);

    void Run();

    static bool IsExit() { return _exit; }

    ExchangeInterface* GetExchange(ExchangeType type);
    ExchangeInterface* GetAvaliableStockExchange();
    ExchangeInterface* GetAvaliableFutureExchange();

    float GetInterestRate(time_t datetime);
    
    //StrategyPlugin* GetOrCreateStrategy(const std::string& strategy_name);

    const ServerConfig& GetConfig() const { return *_config; }
    ServerConfig& GetConfig() { return *_config; }

    IStopLoss* GetStopLoss(const String& name);

    HttpHandler* GetHandler(const String& name);

    RuningType GetRunningMode() { return _runType; }

  std::shared_ptr<DataGroup> PrepareData(const Set<symbol_t>& symbols, DataFrequencyType type, StockAdjustType right = StockAdjustType::None);
  std::shared_ptr<DataGroup> PrepareStockData(const List<String>& symbols, DataFrequencyType type, StockAdjustType right = StockAdjustType::None);

  BrokerSubSystem* GetBrokerSubSystem() { return _brokerSystem; }

  PortfolioSubSystem* GetPortforlioSubSystem() { return _portfolioSystem; }
  StrategySubSystem* GetStrategySystem() { return _strategySystem; }

  double GetFreeRate(time_t t);
  /**
   * @brief Get the Position of Account
   * 
   * @param account 
   * @return AccountPosition& 
   */
  AccountPosition& GetPosition(const String& account = "");

  Set<String> GetAccounts();
  
  /**
   * @brief Get the previous N day's close price
   */
  Vector<double> GetDailyClosePrice(symbol_t symbol, int N, StockAdjustType adjust);

  /**
   */
  double AdjustAfter(symbol_t symbol, double org_price, time_t org_t);
  
  double AdjustBefore(symbol_t symbol, double org_price, time_t org_t);

  double ResetPrice(symbol_t symbol, double adj_price, time_t adj_t);

  bool IsOpen(symbol_t symbol, time_t t);

  bool IsOpen(ExchangeName exchange, time_t);

  time_t GetCloseTime(ExchangeName exchange);

  void SetActiveExchange(ExchangeInterface* exchange) {
    _trade_exchange = exchange;
  }

  bool SendEmail(const String& content);
  // 检查是否在数据备份中
  bool IsDataLock() { return _isDataLock; }
  void LockData() { _isDataLock = true; }
  void FreeDataLock() { _isDataLock = false; }

private:
    void Regist();
    // void RunCammand();

    void RegistAPI();
    // Commander* RegistCommand();
    void RunService();

    void InitStrategy();

    bool InitExchange();

    bool InitDatabase();

    void InitDefault();

    bool InitMarket(const std::string& path);

    bool InitInterestRate(const std::string& path);

    void InitHandlers();

    // 通过历史计算股票日回报率
    bool InitStockDailyReturn(const std::string& path);

    // 从配置中获取最大读取数据量
    int GetMaxPrepareCount();

    bool LoadDataBySymbol(const String& symbol, StockAdjustType right, DataFrequencyType type = DataFrequencyType::Day);

    bool LoadStock(DataFrame& df, const String& path);
    /**
     * @brief load stock last N data
     */
    bool LoadStock(DataFrame& df, symbol_t symbol, int lastN);

    bool LoadFuture(DataFrame& df, const String& path);

    void StartTimer();

    void Timer();

    void Schedules(time_t t);

    void UpdateQuoteQueryStatus(time_t);

    void UpdateNextPrediction(time_t);

    // 发送当日关注的合约的收盘信息作为特征
    void SendCloseFeatures();

    bool JWTMiddleWare(const httplib::Request& req, httplib::Response& res);

    void InitStocks(const String& path);
    void InitStocks();
    void InitFutures(const String& path);
    void InitFutures();
private:
    struct DividendData {
        time_t _start;
        double _recordPrice;
        double _divd;
        double _transf;
        double _bonus;
    };

    bool GetDividendInfo(symbol_t symbol, Map<time_t, DividendData>& dividends_info);

private:
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  // HTTPS
    httplib::SSLServer _svr;
#else
    httplib::Server _svr;
#endif

  static bool _exit;
  bool _isDataLock = false;
  RuningType _runType;
  int _defaultPortfolio;

  Map<String, HttpHandler*> _handlers;

  ServerConfig* _config;

  ExchangeInterface* _trade_exchange;

  StrategySubSystem* _strategySystem;
  PortfolioSubSystem* _portfolioSystem;
  BrokerSubSystem* _brokerSystem; //
  
  // 默认持仓id
  static std::multimap<std::string, ContractInfo> _markets;
  static std::map<time_t, float> _inter_rates;
  // 数据缓存
  Map<String, DataFrame> _data;
  Map<String, DataFrame> _hfqdata;
  List<String> _symbolCache;
  Map<int, DataFrame> _simulations;
  // 除权出息信息
  TickMap<symbol_t, Map<time_t, DividendData>> _dividends;

  std::thread* _timer;

  /**
   * an account only have one holding
   */
  Map<String, AccountPosition> _account_positions;

    Map<ExchangeName, Set<time_range>> _working_times;
};