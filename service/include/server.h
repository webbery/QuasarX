#pragma once
#include "std_header.h"
#include "config.h"
#include "Handler/RiskHandler.h"
#include "Util/system.h"
#include "json.hpp"
#include "HttpHandler.h"
#include "Bridge/exchange.h"
#include <atomic>
#include <ctime>
#include <mutex>
#include <nng/nng.h>
#include <nng/supplemental/http/http.h>
#include "nng/nng.h"
#include "Handler/RecordHandler.h"
#include "StrategySubSystem.h"
#include "Data/DataPipeline.h"
#include "Util/ThreadPool.h"
#include "Util/JobSession.h"

// 策略验证结果结构
struct StrategyValidationResult {
    bool success;
    String errorMessage;
    String errorLocation;  // "buy", "sell", 或 "general"
};

#define HEADER_SIZE         2
#define MAX_HISTORY_SIZE    32
#define MAX_STREAM_SIZE     8192    // 8K

#define ConvertContractType(type) (ContractType)(type & 127)

class Commander;
class StrategySubSystem;
class ExchangeManager;
class StrategyPlugin;
class IStopLoss;
class StopLossHandler;
class BrokerSubSystem;
class RiskSubSystem;

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
    char _type;     // ContractType, 对期权, 首位1表示看涨, 0看跌
    char _market;   // 股票对应的市场类型，如主板、科创板等
    ExchangeName _exchange;
    String _name;
    String _expireDate; //到期日
    String _deliveryDate;   //交割日
    float _strike;  // 行权价
};

enum class DataFrequencyType {
  Day,
  Week,
  Min1,
  Min5,
  Min15,
  Min30,
  Min60,
  Second,
};

// 与 baostock / QuoteDB 的 freq 字符串互转
const char* DataFrequencyTypeToString(DataFrequencyType f);
// 返回 Day 表示无法识别（兜底）
DataFrequencyType DataFrequencyTypeFromString(const char* s);

enum class StockAdjustType {
  None,
  After,
};


class Broker;
class Server {
public:
    // 获取代码对应的市场
    static ExchangeName GetExchange(const std::string& symbol);
    static Pair<ContractType, char> GetContractType(const std::string& symbol, const String& exhange = "");
    static nng_socket GetSocket();
    static String GetName(const String& symbol);
    static const ContractInfo& GetSecurity(const String& symbol);

    // enum EXECUTE_MODE: unsigned short {
    //     MODE_SERVICE,   // 网络服务模式
    //     MODE_COMMAND,   // 本地命令交互模式
    // };
    Server();
    ~Server();

    bool Init(const char* config);

    void Run();

    // 请求停止服务：httplib 的 listen() 返回后，Run() 会走完清场流程并 return，
    // main 随后正常返回 → Server 析构 → ~BrokerSubSystem → Release() →
    // PersistLedger()。账本是在析构里落盘的，所以任何退出路径都必须走
    // "main 正常返回"，不能 _exit()。
    void Stop();

    static bool IsExit() { return _exit; }

    ExchangeInterface* GetExchange(ExchangeType type);

    float GetInterestRate(time_t datetime);

    //StrategyPlugin* GetOrCreateStrategy(const std::string& strategy_name);

    const ServerConfig& GetConfig() const { return *_config; }
    ServerConfig& GetConfig() { return *_config; }

    IStopLoss* GetStopLoss(const String& name);

    HttpHandler* GetHandler(const String& name);

    RuningType GetRunningMode() { return _runType; }
    void SetRunningMode(RuningType mode) { _runType = mode; }

    BrokerSubSystem* GetBrokerSubSystem() { return _brokerSystem; }

    StrategySubSystem* GetStrategySystem() { return _strategySystem; }

    RiskSubSystem* GetRiskSubSystem() { return _riskSystem; }

    ExchangeManager* GetExchangeManager() { return _exchangeMgr; }

    double GetFreeRate(time_t t);

    /**
     * @brief Get the previous N day's close price
     */
    // Vector<double> GetDailyClosePrice(symbol_t symbol, int N, StockAdjustType adjust);

    bool IsOpen(symbol_t symbol, time_t t);

    bool IsOpen(ExchangeName exchange, time_t);

    time_t GetCloseTime(ExchangeName exchange);

    bool SendEmail(const String& content);
    bool SendHtmlEmail(const String& htmlContent);
    bool SendEmail(const String& content, const String& subject);
    bool SendHtmlEmail(const String& htmlContent, const String& subject);
    bool SendMailWithAttachments(const String& body, bool isHtml,
                                 const Vector<String>& attachments,
                                 const String& subject = "");
    // 检查是否在数据备份中
    bool IsDataLock() { return _isDataLock; }
    void LockData() { _isDataLock = true; }
    void FreeDataLock() { _isDataLock = false; }

    void InitMarket(const List<Pair<String, ExchangeName>>& info);

    ExchangeInfo GetExchangeInfo(const String& name);

    // 添加合约信息到市场列表 (由 Exchange 初始化时调用)
    void AddSymbolToMarket(const String& code, ContractInfo&& info);

    // 策略验证：验证策略配置的类型正确性
    // 返回 pair.first 为 true 表示验证通过，false 表示失败
    // pair.second 包含错误信息（如果有）
    std::pair<bool, String> ValidateStrategyConfig(const nlohmann::json& config);

    /// 两融明细下载并入库（供 REST 的 MarginHandler 调用）
    ///
    /// 走 Data/MarginData 这条公共链路：下载 → CSV 级体检 → 分块入库 → DB 级复查。
    /// date 为空表示由脚本自动取最近有数据的交易日；env 是配置里的 python 环境名。
    /// 返回导入行数；<0 表示失败，原因写入 error。
    int DownloadMarginData(const String& date, const String& env, String& error);

    /// 数据任务进度回调：任务内部在阶段边界调用，由 SubmitDataJob 补上 job_id 后
    /// 以 SSE 进度事件发出。payload 的形状归各条链路自己定（行情带 freq，两融带 date）。
    using JobProgress = std::function<void(Map<String, String> payload)>;

    /// 下载并导入一批标的的行情（供 REST 的 QuoteDownloadHandler / OptionDataHandler 调用）
    ///
    /// 走 Data/QuoteData 这条公共链路。symbols 用内部格式（sh.600519），
    /// freq 是 "daily"/"5m"/"15m"/"30m"/"60m"。overwrite=true 时无视库里的最新日期全量重下。
    /// progress 可为空 —— 内部自己推 SSE 的调用方（OptionDataHandler）不需要它。
    /// 返回导入行数；<0 表示失败。
    ///
    /// outFailedSymbols 非空时回填本轮体检未通过、没入库的标的（去重后）。
    /// 不给的话，部分标的体检没过会被当成完全成功 —— 前端进度卡会说「完成」，
    /// 用户以为数据齐了。
    int DownloadQuoteData(const Vector<symbol_t>& symbols, const String& freq,
                          bool overwrite, const String& env,
                          const JobProgress& progress = nullptr,
                          Vector<String>* outFailedSymbols = nullptr);

    /// 异步下载并导入一批标的的行情（内部提交到数据线程池）。
    /// REST 的 QuoteDownloadHandler / OptionDataHandler 用这个，不阻塞请求线程。
    ///
    /// 返回的票据带三样东西：生效的 job_id（前端靠它订阅终态事件）、
    /// 本次是否复用了已在跑的任务、以及被跳过的标的（已在别的任务里下载）。
    /// 少了复用这一路：同一批标的并发下两次会同时写同一个 CSV 和同一张表。
    DataJobDispatch SubmitQuoteDownload(const Vector<symbol_t>& symbols, const String& freq,
                                        bool overwrite, const String& env);

    /// 任务会话表：按 job_id 查任务状态（推送漏了也能查回来）
    JobSessionStore& GetJobSessions() { return *_jobSessions; }

    /// 异步下载并导入两融明细（供 REST 的 MarginHandler）。
    /// date 为空 = 由脚本自动取最近有数据的交易日。
    void SubmitMarginDownload(const String& date, const String& env);

    /// 异步下载并导入基本面数据（供 REST 的 FinanceHandler）。
    /// categories 为空表示全部六个类目；start/end 用脚本原生格式
    /// （"2022" 或 "2022-Q1"），空 = 脚本默认。
    void SubmitFundamentalDownload(const Vector<symbol_t>& symbols,
                                   const Vector<DataCategory>& categories,
                                   const String& start, const String& end,
                                   const String& env);

    // type 0-股票
    // StockHistorySimulation* CreateSimulation(const String& name, const String& strategy, int type = 0);
private:
    void Regist();
    // void RunCammand();

    void RegistAPI();

    bool InitDatabase();

    void InitDefault();

    bool InitMarket(const std::string& path);

    bool InitInterestRate(const std::string& path);

    void InitHandlers();

    // 从配置中获取最大读取数据量
    int GetMaxPrepareCount();


    void StartTimer();

    void Timer();

    void Schedules(time_t t);

    /// 日终数据刷新入口：行情 / 两融 / 基本面三条链路
    ///
    /// 触发条件不再是「到了 20:00 这一分钟」，而是「目标交易日还没刷过」：
    /// 昨天失败、进程晚启动、20:00 那分钟重启过，都会在下一个 tick 自动补上。
    /// 具体三条链路各自提交到数据线程池，不阻塞定时器线程。
    void RefreshDailyData();

    /// 凌晨补数据入口：把上一轮体检遗留的异常重新下载并入库
    void RepairDailyData();

    /// 三类数据依次补一遍（由 RepairDailyData 在数据线程池里调用）
    void RunDataRepair(const String& targetDay);

    /// 把一条数据任务交给数据线程池。
    ///
    /// 同一个标的只允许一个任务在下载：requested 里已经在同频任务中下载的标的
    /// 会被跳过（记在返回值的 _skipped 里），全部都在跑时直接复用那个任务、
    /// 返回它的 job_id —— 前端靠 job_id 订阅终态事件，发一个永远不会有事件的 id
    /// 会让进度卡片永远转圈。
    ///
    /// toDownload 由本函数填（请求减去已在跑的），派发是异步的，调用方在
    /// 回调里读它。
    DataJobDispatch DispatchDataJob(const String& key, JobId jobId,
                                    const String& freq, const Set<symbol_t>& requested,
                                    const std::shared_ptr<Set<symbol_t>>& toDownload,
                                    std::function<void()> job);

    /// 不带标的的链路（两融 / 基本面 / 日终刷新）：没有标的可跳
    DataJobDispatch DispatchDataJob(const String& key, JobId jobId,
                                    std::function<void()> job);

    /// 数据任务结果：三条链路共用，避免 SubmitXxx 各写一套 int/error/issues 约定
    struct DataJobResult {
        int    rows = -1;    ///< 导入行数，<0 = 失败
        size_t issues = 0;   ///< 本轮体检异常数
        String error;        ///< 失败原因（rows < 0 时使用）
    };

    /// 数据任务的统一提交骨架：数据线程池（同 key 去重）+ SSE 开始/结束事件
    /// + 异常兜底。
    ///
    /// 具体链路差异（构造哪个 Data 子类、配置哪些参数、跑哪个目录、事件
    /// payload 长什么样）由 run / finished 两个回调承载 —— 这正是为什么
    /// 这里用回调而不是模板类：差异在配置与事件协议，不在数据类型本身。
    ///
    /// @param key      去重键（同一 key 同时只允许一个任务）
    /// @param event    SSE 事件名（quote_download / margin_download / ...）
    /// @param started  开始事件 payload；空表示不发开始事件
    /// @param finished 结束事件 payload；rows < 0 时由各链路映射成自己的失败格式
    /// @param run      真正干活：跑完返回 DataJobResult
    /// @return         job_id。开始/进度/结束事件都会带上它，前端据此把同一条链路的
    ///                 事件关联起来 —— 只靠 freq 关联的话，同频率并发两个下载会串。
    ///
    /// run 有两个重载：需要中间进度的链路用带 JobProgress 的那个，不需要的原样传。
    JobId SubmitDataJob(const String& key, const String& event,
                        Map<String, String> started,
                        std::function<Map<String, String>(const DataJobResult&)> finished,
                        std::function<DataJobResult()> run);

    JobId SubmitDataJob(const String& key, const String& event,
                        Map<String, String> started,
                        std::function<Map<String, String>(const DataJobResult&)> finished,
                        std::function<DataJobResult(const JobProgress&)> run);

    /// 带标的覆盖去重的提交骨架（行情链路用）
    ///
    /// requested 里的标的若已在同频任务中下载，会被跳过；全部都在跑时复用那个
    /// 任务。run 收到的第二个参数是本次真正要下载的标的。
    ///
    /// 任务状态记进 JobSessionStore：推送漏了也能按 job_id 查回来。
    DataJobDispatch SubmitDataJob(const String& key, const String& event, const String& freq,
                                  const Set<symbol_t>& requested,
                                  const std::shared_ptr<Set<symbol_t>>& toDownload,
                                  Map<String, String> started,
                                  std::function<Map<String, String>(const DataJobResult&)> finished,
                                  std::function<DataJobResult(const JobProgress&, const Set<symbol_t>&)> run);

    /// 行情链路（按频率逐个跑 QuoteData，串行以免同时拉起多个 python 进程）
    void RunQuoteRefresh(const String& targetDay);

    /// 两融链路
    void RunMarginRefresh(const String& targetDay);

    /// 基本面链路
    void RunFundamentalRefresh(const String& targetDay);

    /// 收集所有活跃策略的标的池（去重）
    Vector<symbol_t> CollectStrategySymbols();

    /// 刷新融资融券（可做空）标的名单：拉 tools/fetch_shortable_stocks.py 并全量重写
    /// FinanceDB::margin_stock。由 Schedules 在每周六的日终时点调用。
    ///
    /// SignalNode 只读这张表，不自行抓取 —— 全局数据不该挂在单策略的生命周期上。
    /// 必须由服务进程写：DuckDB 单写者，服务持有 finance.db 后外部脚本打不开。
    /// 抓取或解析失败时保留原名单（importMarginStocks 对空输入不清表）。
    void RefreshMarginStockList(nng_socket sseSock);

    void TimerWorker(nng_socket sock);

    void UpdateQuoteQueryStatus(time_t);

    void UpdateNextPrediction(time_t);

    // 发送当日关注的合约的收盘信息作为特征
    // void SendCloseFeatures();

    bool JWTMiddleWare(const httplib::Request& req, httplib::Response& res);

    // 邮件发送内部实现：写临时文件 + 拼命令 + RunCommand + 清理
    bool SendMailInternal(const String& body, bool isHtml,
                          const String& subject,
                          const Vector<String>& attachments);

    void InitStocks(const String& path);
    void InitStocks();
    void InitFutures(const String& path);
    void InitFutures();

    void ReloadMarketData(const String& path);

private:
    static ExchangeName GetExchangeName(const String& prefix);
    
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

    ExchangeManager* _exchangeMgr = nullptr;

    StrategySubSystem* _strategySystem;
    BrokerSubSystem* _brokerSystem; //
    RiskSubSystem* _riskSystem;

    // 默认持仓id
    static std::multimap<std::string, ContractInfo> _markets;
    static std::map<time_t, float> _inter_rates;

    // 数据缓存
    List<String> _symbolCache;

    std::thread* _timer;

    Map<ExchangeName, Set<time_range>> _working_times;

    // ── 数据刷新（下载 / 体检 / 导入）──
    //
    // 独立线程池：基本面刷新要按 类目 × 标的 串行跑几百个 python 进程，
    // 和行情下载共用一个池会把队列占死。
    //
    // hardware_concurrency 个 worker，再开多了就是多个 python3 抢 CPU。
    // 规则：≤4 核固定 1 个（三条链路串行排队），更多核才给 2 个。
    std::unique_ptr<ThreadPool> _dataPool;
    // 每条链路最后刷新的目标交易日（持久化，进程重启后仍能判断要不要补）
    std::unique_ptr<RefreshState> _refreshState;
    // 任务会话表：job_id → 会话（事件日志 + 可查快照）
    std::unique_ptr<JobSessionStore> _jobSessions;
    // 已在跑的链路 key → {job_id, 覆盖的标的}。
    // 避免 5 秒一个 tick 把同一个任务反复派发；带上 job_id 是为了让重复提交
    // 能复用进行中任务的 id（前端据此收到终态事件）；带上覆盖的标的才能判断
    // 「这批数据已经在下载了」。
    std::mutex _dataInflightMtx;
    struct DataInflightEntry {
        JobId _jobId = 0;
        String _freq;
        Set<symbol_t> _symbols;
    };
    Map<String, DataInflightEntry> _dataInflight;
};
