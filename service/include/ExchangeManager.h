#pragma once
#include "Bridge/exchange.h"
#include "Util/system.h"
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
     * @brief 按类型获取 Exchange (如 EX_CTP, EX_HX)
     */
    ExchangeInterface* GetExchangeByType(ExchangeType type) const;

    /**
     * @brief 获取活跃的股票交易 Exchange
     */
    ExchangeInterface* GetActiveStockExchange() const;

    /**
     * @brief 获取活跃的期货交易 Exchange
     */
    ExchangeInterface* GetActiveFutureExchange() const;

    // ========== 设置活跃交易所 ==========

    void SetActiveStockExchange(const String& name);
    void SetActiveFutureExchange(const String& name);
    String GetActiveStockName() const;
    String GetActiveFutureName() const;

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

private:
    /**
     * @brief 根据名称从 Server 配置中获取 ExchangeInfo
     */
    ExchangeInfo GetExchangeInfo(const String& name) const;

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
};
