#pragma once
#include "DataHandler.h"
#include "Handler/RiskHandler.h"
#include "Util/system.h"
#include "json.hpp"
#include "std_header.h"
#include "HttpHandler.h"
#include "config.h"
#include "Bridge/exchange.h"
#include <ctime>
#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include "nng/nng.h"
#include "Handler/TimerHandler.h"
#include "StrategySubSystem.h"

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
class VirtualBroker;
class RiskSubSystem;
class TraderSystem;

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

enum class DataRightType {
  None,
  After,
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

    bool IsExit() { return _exit; }

    ExchangeInterface* GetExchange(ExchangeType type);

    float GetInterestRate(time_t datetime);
    
    //StrategyPlugin* GetOrCreateStrategy(const std::string& strategy_name);

    const ServerConfig& GetConfig() const { return *_config; }
    ServerConfig& GetConfig() { return *_config; }

    IStopLoss* GetStopLoss(const String& name);

    HttpHandler* GetHandler(const String& name);

    bool IsReal() { return _is_real; }

  std::shared_ptr<DataGroup> PrepareData(const Set<symbol_t>& symbols, DataFrequencyType type, DataRightType right = DataRightType::None);
  std::shared_ptr<DataGroup> PrepareStockData(const List<String>& symbols, DataFrequencyType type, DataRightType right = DataRightType::None);

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
  
public:
  void SetActiveExchange(ExchangeInterface* exchange) {
    _trade_exchange = exchange;
  }

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

    bool LoadDataBySymbol(const String& symbol, DataRightType right, DataFrequencyType type = DataFrequencyType::Day);

    bool LoadStock(DataFrame& df, const String& path);

    bool LoadFuture(DataFrame& df, const String& path);

    void StartTimer();

    void Timer();

    void Schedules(time_t t);

    void UpdateQuoteQueryStatus(time_t);

    void UpdateNextPrediction(time_t);
private:
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
  // HTTPS
// httplib::SSLServer _svr;
#else
  httplib::Server _svr;
#endif

  bool _exit;
  bool _is_real = false;
  int _defaultPortfolio;

  Map<String, HttpHandler*> _handlers;

  ServerConfig* _config;

  ExchangeInterface* _trade_exchange;

  RiskSubSystem* _riskSystem;
  StrategySubSystem* _strategySystem;
  PortfolioSubSystem* _portfolioSystem;
  BrokerSubSystem* _brokerSystem; // 实盘
  
  TraderSystem* _traderSystem;
  // 默认持仓id
  static std::multimap<std::string, ContractInfo> _markets;
  static std::map<time_t, float> _inter_rates;
  // 数据缓存
  Map<String, DataFrame> _data;
  Map<String, DataFrame> _hfqdata;
  List<String> _symbolCache;
  Map<int, DataFrame> _simulations;

  std::thread* _timer;

  /**
   * an account only have one holding
   */
  Map<String, AccountPosition> _account_positions;
};