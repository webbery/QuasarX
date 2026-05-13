#pragma once
#include "Bridge/exchange.h"
#include "Bridge/SIM/StockPositionManager.h"
#include "httplib.h"
#include <atomic>
#include <mutex>

class TickFlowBridge : public ExchangeInterface {
public:
    TickFlowBridge(Server* server);
    ~TickFlowBridge() override;

    // ExchangeInterface 接口
    const char* Name() override;
    bool Init(const ExchangeInfo& handle) override;
    void SetFilter(const QuoteFilter& filter) override;
    bool Release() override;

    // 登录/登出（空操作）
    bool Login(AccountType t = AccountType::MAIN) override { return true; }
    bool IsLogin() override { return _login_success; }
    void Logout(AccountType t = AccountType::MAIN) override {}

    // 资金/持仓（模拟）
    double GetAvailableFunds(run_id_t run_id) override;
    AccountAsset GetAsset() override;
    bool GetPosition(AccountPosition& pos) override;

    // 订单（模拟成交）
    order_id AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order) override;
    void OnOrderReport(order_id id, const TradeReport& report) override;
    Boolean CancelOrder(order_id id, OrderContext* order) override { return true; }
    bool GetOrders(SecurityType type, OrderList& ol) override { return true; }
    bool GetOrder(const String& sysID, Order& ol) override { return false; }

    // 行情
    void QueryQuotes() override;
    void StopQuery() override;
    QuoteInfo GetQuote(symbol_t symbol) override;

    // 其他
    bool GetCommission(symbol_t symbol, List<Commission>& comms) override { return false; }
    Boolean HasPermission(symbol_t symbol) override { return true; }
    void Reset() override {}
    int GetStockLimitation(char type) override { return 0; }
    bool SetStockLimitation(char type, int limitation) override { return true; }
    void GetFee(FeeInfo& fee, symbol_t symbol) override {}

    bool GetSymbolExchanges(List<Pair<String, ExchangeName>>& info) override { return false; }

private:
    // HTTP 请求
    void FetchQuotes();
    void ParseResponse(const String& response);

    // 符号转换
    String SymbolToTickFlow(symbol_t s);
    symbol_t TickFlowToSymbol(const String& code);

    // 错误处理
    void HandleApiError(const String& reason);

    // 构建 HTTP 客户端
    void InitHttpClient();

private:
    String _api_key;
    String _base_url;
    Vector<String> _universes;
    int _interval_ms;

    // 行情缓存
    ConcurrentMap<symbol_t, QuoteInfo> _quotes;

    // 符号映射表
    Map<symbol_t, String> _symbol_to_code;
    Map<String, symbol_t> _code_to_symbol;

    // 请求频率控制
    std::chrono::steady_clock::time_point _last_request;
    std::atomic<bool> _login_success;

    // HTTP 客户端
    std::unique_ptr<httplib::Client> _http_client;

    // 错误处理状态
    int _error_count;
    int _pause_phase;  // 0=正常, 1=暂停1min, 2=暂停5min, 3=永久暂停
    std::atomic<bool> _paused;
    std::chrono::steady_clock::time_point _pause_until;
    std::mutex _error_mutex;

    // 模拟持仓
    StockPositionManager _positionMgr;
};
