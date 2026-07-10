#pragma once
#include "Bridge/exchange.h"
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Util/system.h"
#include "std_header.h"
#include <nng/nng.h>
#include <mutex>

class Server;

/**
 * @brief 交易所统一管理器
 *
 * 职责:
 *  1. Exchange 实例生命周期管理 (创建/初始化/登录/销毁)
 *  2. 统一行情发布 (单 Pub socket，避免多 Exchange 冲突)
 *  3. 交易路由 (按标的类型路由到正确的 Exchange)
 *  4. 查询聚合 (合约列表、行情快照等)
 */
class ExchangeManager {
public:
    explicit ExchangeManager(Server* server);
    ~ExchangeManager();

    // ========== 初始化 ==========

    /**
     * @brief 初始化统一行情发布通道 (在 Server 启动时调用)
     */
    bool InitQuotePublisher();

    // ========== Exchange 生命周期管理 ==========

    /**
     * @brief 从配置注册并初始化 Exchange（等价于原 ExchangeHandler::Use）
     * @param name 配置中的交易所名称
     */
    bool Use(const String& name);

    /**
     * @brief 注册并初始化一个 Exchange
     * @param name  配置中的交易所名称 (如 "ctp", "hx")
     * @param type  交易所类型枚举
     * @return 是否成功
     */
    bool RegisterExchange(const String& name, ExchangeType type);

    /**
     * @brief 注销指定 Exchange
     */
    void UnregisterExchange(const String& name);

    /**
     * @brief 注销所有 Exchange 并释放资源
     */
    void Shutdown();

    // ========== 策略级生命周期 ==========

    /**
     * @brief 启动 Exchange（Login + 开始行情查询）
     *
     * 内部维护引用计数：首次启动时 Login，后续调用只增加计数
     */
    bool StartExchange(const String& name);

    /**
     * @brief 停止 Exchange（StopQuery + Logout）
     *
     * 内部维护引用计数：计数归零时才真正 Logout
     */
    bool StopExchange(const String& name);

    /**
     * @brief 根据策略图中的 source 参数，启动所有需要的 Exchange
     *
     * 内部根据配置判断使用历史数据还是真实数据
     * @param requiredSources 数据源集合，如 {"股票"} 或 {"股票", "ETF"}
     */
    bool StartRequiredExchanges(const Set<String>& requiredSources);

    /**
     * @brief 停止与指定数据源相关的 Exchange（引用计数减一）
     *
     * 与 StartRequiredExchanges 对称，只停止该策略启动的 Exchange
     */
    void StopRequiredExchanges(const Set<String>& requiredSources);

    /**
     * @brief 停止所有已启动的 Exchange（引用计数归零）
     */
    void StopAllExchanges();

    // ========== 策略级行情订阅 ==========

    /**
     * @brief 策略启动时调用：将策略标的注入对应 source 的 Exchange
     *
     * 内部维护 per-symbol 引用计数，多策略共享同一 symbol 时只增不减，
     * 仅当引用计数归零才真正从 Exchange 移除订阅
     * @param strategy 策略名称
     * @param sources 数据源集合 {"股票"/"ETF"}
     * @param symbols 策略的 symbol_t 集合
     */
    void SubscribeSymbols(const String& strategy, const Set<String>& sources, const Set<symbol_t>& symbols);

    /**
     * @brief 策略停止时调用：移除该策略的标的订阅
     *
     * 只有当 symbol 不再被任何策略使用时才调用 Exchange::RemoveSymbols
     */
    void UnsubscribeSymbols(const String& strategy, const Set<String>& sources);

    // ========== 查询接口 ==========

    /**
     * @brief 获取指定名称的 Exchange
     */
    ExchangeInterface* GetExchange(const String& name) const;

    /**
     * @brief 获取所有已注册的 Exchange (按名称)
     */
    const Map<String, ExchangeInterface*>& GetAllExchanges() const;

    /**
     * @brief 获取所有已注册的 Exchange (按类型)
     */
    const Map<ExchangeType, ExchangeInterface*>& GetExchangesByType() const;

    /**
     * @brief 获取所有当前有效的 Exchange 实例
     * @return 所有已注册且非空的 Exchange 指针列表
     */
    Vector<ExchangeInterface*> GetActiveExchanges() const;

    /**
     * @brief 按类型获取 Exchange (如 EX_CTP, EX_HX)
     */
    ExchangeInterface* GetExchangeByType(ExchangeType type) const;

    /**
     * @brief 确保指定类型的 Exchange 已注册，不存在时自动创建默认实例
     *
     * 首先从 config.json 中查找对应 API 类型的 exchange 名称，
     * 如果未找到则使用默认名称创建
     * @param type 交易所类型
     * @return 是否成功
     */
    bool EnsureExchangeByType(ExchangeType type);

    /**
     * @brief 获取活跃的股票交易 Exchange
     */
    ExchangeInterface* GetActiveStockExchange() const;

    /**
     * @brief 获取活跃的期货交易 Exchange
     */
    ExchangeInterface* GetActiveFutureExchange() const;

    // ========== 交易相关（持仓/订单） ==========

    /**
     * @brief 从所有交易 Exchange 获取汇总持仓信息
     * @param outPosition 输出参数，填充所有持仓
     * @return 是否获取到持仓
     */
    bool GetTradingPosition(AccountPosition& outPosition) const;

    /**
     * @brief 从所有交易 Exchange 获取汇总订单信息
     * @param secType 证券类型
     * @param outOrders 输出参数，填充所有订单
     * @return 是否获取到订单
     */
    bool GetTradingOrders(SecurityType secType, OrderList& outOrders) const;

    // ========== 活跃交易所 ==========
    Vector<ExchangeInterface*> GetActiveExchanges();

    // ========== 行情发布 ==========

    /**
     * @brief 启动行情分发线程 (在 Server::Init 时调用)
     *
     * 该线程是唯一创建/监听/发送 nng pub socket 的地方，
     * 所有其他 Exchange 通过 QueueToPublish 将行情推入队列，
     * 由该线程统一 nng_send，避免多 pub socket 冲突。
     */
    bool StartQuoteDispatcher();

    /**
     * @brief 停止行情分发线程并等待清理 (在 Server 析构时调用)
     */
    void StopQuoteDispatcher();

    /**
     * @brief 线程安全地将行情推入分发队列 (供各 Exchange 调用)
     * @param quote 行情数据
     *
     * 该函数可以从任意线程调用（如 TickFlowBridge worker、HX 回调线程等），
     * 内部通过 mutex + condition_variable 推入队列，由 dispatch 线程统一发送。
     */
    void QueueToPublish(const QuoteInfo& quote);

    // ========== 交易路由 ==========

    // ========== 定时任务 ==========

    /**
     * @brief 更新行情查询状态 (定时器调用)
     *
     * 遍历所有 Exchange，根据工作状态调用 QueryQuotes 或 Login
     */
    void UpdateQuoteQueryStatus(time_t curr);

    // ========== 配置查询 ==========

    bool IsSimulationEnabled() const;

    // ========== 多回测引擎协调 ==========

    /**
     * @brief 按类型列表获取 Exchange 实例
     */
    Vector<ExchangeInterface*> GetExchangesByTypes(const Vector<ExchangeType>& types) const;

    /**
     * @brief 根据标的类型路由到对应的 Exchange
     *
     * 规则: is_fund(symbol) → 优先 ETFExchange, 否则 StockExchange
     *       is_stock(symbol) → StockExchange
     */
    ExchangeInterface* ResolveExchange(const symbol_t& symbol) const;

    /**
     * @brief 对需要的 Exchange 调用 Login
     * @param requiredSources 需要的数据源集合 {"股票", "ETF"}
     */
    bool LoginExchanges(const Set<String>& requiredSources);

    /**
     * @brief 对所有历史回测 Exchange 调用 Logout
     */
    void LogoutExchanges();

    /**
     * @brief 对所有历史回测 Exchange 推进一步
     * @return 是否还有数据 (false = 全部完成)
     */
    bool StepAllHistoryExchanges(run_id_t runId);

    /**
     * @brief 创建回测上下文，根据标的类型分配到对应 Exchange
     * @return 主 Exchange 分配的 runId
     */
    run_id_t CreateMultiContext(const String& strategy,
                                 const Set<symbol_t>& symbols,
                                 double initialCapital);

    /**
     * @brief 汇总所有历史回测 Exchange 的可用资金
     */
    double GetTotalAvailableFunds(run_id_t runId) const;

    /**
     * @brief 获取所有历史回测 Exchange 的进度（取主 Exchange 的进度）
     */
    double GetProgress(const String& strategy) const;

    /**
     * @brief 对所有历史回测 Exchange 设置回测时间范围
     */
    void SetBacktestTimeRange(time_t start, time_t end);

    /**
     * @brief 从 config.json 加载 etf.t0/etf.t1 并设置到 ETFHistorySimulation
     */
    void ConfigureETFExchange();

    // ========== 滑点模型配置 ==========

    /**
     * @brief 根据策略的数据源配置滑点模型
     *
     * 由策略解析层调用，根据 sources 自动路由到对应的 HistorySimulation Exchange
     * @param sources 数据源集合，如 {"股票"} 或 {"ETF"}
     * @param slippageConfig 滑点模型 JSON 配置（由 SlippageFactory::create 解析的格式）
     */
    void ConfigureSlippageModels(const Set<String>& sources, const nlohmann::json& slippageConfig);

    order_id AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order);

private:
    /**
     * @brief 根据名称从 Server 配置中获取 ExchangeInfo
     */
    ExchangeInfo GetExchangeInfo(const String& name) const;

    /**
     * @brief 注册一个已创建的 Exchange 实例（内部调用）
     */
    void RegisterExchangePtr(const String& name, ExchangeType type, ExchangeInterface* ptr);

    /**
     * @brief 根据配置和 source 参数，确定应启动的 Exchange 名称
     */
    Set<String> ResolveExchangeNames(const Set<String>& requiredSources);

    void quoteDispatchLoop();
private:
    Server* _server;

    // Exchange 容器
    Map<String, ExchangeInterface*> _exchanges;          // 按名称索引
    Map<ExchangeType, ExchangeInterface*> _typeExchanges; // 按类型索引

    // 活跃交易所名称
    String _activeStockName;
    String _activeFutureName;

    // 统一行情分发（单线程，确保 nng pub socket 的 init/send/close 在同一线程）
    nng_socket              _quotePubSock{0};
    bool                    _quotePubInited = false;
    std::mutex              _pubQueueMtx;
    std::condition_variable _pubQueueCV;
    Vector<QuoteInfo>       _pubQueue;       // 待发布的行情缓冲区
    std::thread*            _dispatchThread = nullptr;
    std::atomic<bool>       _dispatchRunning = false;

    // 模拟模式标志
    bool _enableSimulation = false;

    // 引用计数（多策略并发安全）
    Map<String, int> _exchangeRefCounts;

    // 策略级行情订阅追踪
    Map<String, Set<String>> _strategySubscriptions;   // strategy → symbol strings
    Map<String, int>         _symbolRefCounts;          // symbol string → refcount
    std::mutex               _subMtx;
};
