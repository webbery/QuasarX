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
     * @brief 统一行情发布接口 (供各 Quote SPI 调用)
     * @param data  序列化后的 QuoteInfo 数据指针
     * @param size  数据大小
     *
     * 线程安全: 内部加锁保护 Pub socket
     */
    void PublishQuote(const void* data, size_t size);

    // ========== 交易路由 ==========

    /**
     * @brief 买入 (自动路由到正确的 Exchange)
     */
    double Buy(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& deals);

    /**
     * @brief 卖出 (自动路由到正确的 Exchange)
     */
    double Sell(const String& strategy, symbol_t symbol, const Order& order, TradeInfo& deals);

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

private:
    Server* _server;

    // Exchange 容器
    Map<String, ExchangeInterface*> _exchanges;          // 按名称索引
    Map<ExchangeType, ExchangeInterface*> _typeExchanges; // 按类型索引

    // 活跃交易所名称
    String _activeStockName;
    String _activeFutureName;

    // 统一行情发布
    nng_socket _quotePubSock;
    bool _quotePubInited = false;
    std::mutex _quotePubMtx;   // 保护 _quotePubSock

    // 模拟模式标志
    bool _enableSimulation = false;

    // 引用计数（多策略并发安全）
    Map<String, int> _exchangeRefCounts;
};
