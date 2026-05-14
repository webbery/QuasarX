#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "std_header.h"
#include "Bridge/TickFlow/TickFlowBridge.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Util/system.h"
#include "json.hpp"
#include "server.h"
#include <format>
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/x509.h>
#endif

TickFlowBridge::TickFlowBridge(Server* server)
    : ExchangeInterface(server),
      _base_url("https://api.tickflow.org"),
      _universes{"CN_Equity_A"},
      _interval_ms(9500),
      _login_success(false),
      _error_count(0),
      _pause_phase(0),
      _paused(false),
      _positionMgr(BACKTEST_INITIAL_CAPITAL) {
}

TickFlowBridge::~TickFlowBridge() {
}

const char* TickFlowBridge::Name() {
    return "TickFlowBridge";
}

bool TickFlowBridge::Init(const ExchangeInfo& handle) {
    // API Key 拼接：_username 存前缀（如 "tk_"），_passwd 存 32 位主体
    // 完整 token 形如: tk_9f008d2914ab4dbda62ac83a063ff6f6
    _api_key = String(handle._username) + String(handle._passwd, 32);
    if (_api_key.empty()) {
        FATAL("TickFlow API key is empty, bridge disabled");
        return false;
    }

    _base_url = "https://api.tickflow.org";
    // _universes = {"CN_Equity_A"};
    _interval_ms = 9500;

    InitHttpClient();
    _login_success = true;

    INFO("TickFlowBridge initialized, api_key loaded, interval={}ms", _interval_ms);
    return true;
}

void TickFlowBridge::SetFilter(const QuoteFilter& filter) {
    _filter = filter;

    // 初始化符号映射表
    for (const auto& code : filter._symbols) {
        symbol_t sym = TickFlowToSymbol(code);
        if (is_null(sym)) {
            _symbol_to_code[sym] = code;
            _code_to_symbol[code] = sym;
        }
    }
    INFO("SetFilter: {} symbols loaded", filter._symbols.size());
}

bool TickFlowBridge::Release() {
    _login_success = false;
    _quotes.clear();
    return true;
}

void TickFlowBridge::InitHttpClient() {
    _http_client = std::make_unique<httplib::Client>("https://api.tickflow.org");
    _http_client->set_connection_timeout(10);
    _http_client->set_read_timeout(15);
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#ifdef _WIN32
    // Windows: 从系统证书存储加载根证书到 X509_STORE
    auto* store = X509_STORE_new();
    if (store && httplib::detail::load_system_certs_on_windows(store)) {
        _http_client->set_ca_cert_store(store);
        //INFO("SSL: loaded {} certs from Windows system store",
        //     X509_STORE_get_num_certificates(store));
    } else {
        if (store) X509_STORE_free(store);
        WARN("SSL: failed to load Windows system certificates");
    }
#else
    const char* ca_path = "/etc/ssl/certs/ca-certificates.crt";
    if (std::filesystem::exists(ca_path)) {
        _http_client->set_ca_cert_path(ca_path);
        INFO("SSL CA cert loaded from: {}", ca_path);
    } else {
        WARN("SSL CA cert file not found: {}. SSL verification may fail.", ca_path);
    }
#endif
#endif
}

// ==================== 符号转换 ====================

String TickFlowBridge::SymbolToTickFlow(symbol_t s) {
    auto it = _symbol_to_code.find(s);
    if (it != _symbol_to_code.end()) {
        return it->second;
    }
    // 回退：手动构造
    const Map<char, String> exchange_rev = {
        {MT_Shenzhen, "SZ"}, {MT_Shanghai, "SH"}, {MT_Beijing, "BJ"}
    };
    char exchange_code[3] = "SH";
    auto exc_it = exchange_rev.find(s._exchange);
    if (exc_it != exchange_rev.end()) {
        exchange_code[0] = exc_it->second[0];
        exchange_code[1] = exc_it->second[1];
    }
    return std::format("{:06d}.{}", (uint32_t)s._symbol, exchange_code);
}

symbol_t TickFlowBridge::TickFlowToSymbol(const String& code) {
    auto it = _code_to_symbol.find(code);
    if (it != _code_to_symbol.end()) {
        return it->second;
    }

    // TickFlow 格式: 600000.SH -> 翻转为 SH.600000 再调用 to_symbol
    List<String> tokens;
    split(code, tokens, ".");
    String adapted = (tokens.size() == 2) ? (tokens.back() + "." + tokens.front()) : code;
    symbol_t sym = to_symbol(adapted);

    // 缓存映射
    _code_to_symbol[code] = sym;
    _symbol_to_code[sym] = code;
    return sym;
}

// ==================== 资金/持仓 ====================

double TickFlowBridge::GetAvailableFunds(run_id_t run_id) {
    return _positionMgr.GetAvailableFunds();
}

AccountAsset TickFlowBridge::GetAsset() {
    return _positionMgr.GetAsset();
}

bool TickFlowBridge::GetPosition(AccountPosition& pos) {
    return _positionMgr.GetPosition(pos);
}

// ==================== 订单模拟成交 ====================

order_id TickFlowBridge::AddOrder(run_id_t run_id, const symbol_t& symbol, OrderContext* order) {
    String side = order->_order._side == 0 ? "BUY" : "SELL";
    String symbol_str = SymbolToTickFlow(symbol);

    INFO("Order (no-op): {} {} @ {:.4f} volume={}", side, symbol_str, order->_order._price, order->_order._volume);

    // 更新模拟持仓
    double price = order->_order._price;
    int64_t qty = order->_order._volume;
    if (order->_order._side == 0) {
        _positionMgr.Buy(symbol, qty, price);
    } else {
        _positionMgr.Sell(symbol, qty, price);
    }

    // 构造成交回报
    TradeReport report{};
    report._status = OrderStatus::OrderSuccess;
    report._price = price;
    report._quantity = qty;
    report._time = std::time(nullptr);
    report._type = static_cast<char>(OrderType::Limit);
    report._side = order->_order._side;
    report._flag = order->_order._flag;

    // 触发回调和邮件通知
    OnOrderReport({}, report);

    // 标记订单成功
    order->_flag = true;
    order->_success = true;
    try {
        order->_promise.set_value(true);
    } catch (...) {}

    return order_id{};
}

void TickFlowBridge::OnOrderReport(order_id id, const TradeReport& report) {
    // 邮件通知（持仓已在 AddOrder 中更新）
    if (_server) {
        String content;
        if (report._side == 0) {
            content = std::format("Buy {} {}", report._price, report._quantity);
        } else {
            content = std::format("Sell {} {}", report._price, report._quantity);
        }
        if (!content.empty()) {
            _server->SendEmail(content);
        }
    }
}

// ==================== 行情 ====================

QuoteInfo TickFlowBridge::GetQuote(symbol_t symbol) {
    QuoteInfo quote{};
    _quotes.visit(symbol, [&quote](std::pair<const symbol_t, QuoteInfo>& entry) {
        quote = entry.second;
    });
    return quote;
}

void TickFlowBridge::QueryQuotes() {
    // 频率控制：检查距离上次请求的时间间隔
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_request).count();
    if (elapsed < _interval_ms) {
        return;  // 未到指定间隔，跳过
    }
    _last_request = now;
    
    FetchQuotes();
}

void TickFlowBridge::StopQuery() {
    // 由外部调度，无需内部停止
}

// ==================== HTTP 请求 ====================

void TickFlowBridge::FetchQuotes() {
    if (!_login_success || _filter._symbols.empty()) return;

    // 检查暂停状态
    {
        std::lock_guard<std::mutex> lock(_error_mutex);
        if (_paused) {
            auto now = std::chrono::steady_clock::now();
            if (now < _pause_until) {
                return;  // 仍在暂停中
            } else {
                _paused = false;
                _error_count = 0;
                INFO("Pause expired, resuming TickFlow requests");
            }
        }
    }

    constexpr int BATCH_SIZE = 5;
    auto symbols = Vector<String>(_filter._symbols.begin(), _filter._symbols.end());

    for (size_t i = 0; i < symbols.size(); i += BATCH_SIZE) {
        Vector<String> batch(symbols.begin() + i,
                            symbols.begin() + std::min(i + BATCH_SIZE, symbols.size()));

        // 构建请求体
        nlohmann::json body;
        body["symbols"] = batch;
        // body["universes"] = _universes;

        String body_str = body.dump();
        INFO("POST: {}", body_str);

        // 发送 POST 请求
        httplib::Headers headers = {
            {"Content-Type", "application/json"},
            { "x-api-key", _api_key }
        };
        auto res = _http_client->Post("/v1/quotes", headers, body_str.c_str(), body_str.size(), "application/json");

        if (!res) {
            // 打印详细错误信息
            auto err = res.error();
            const char* err_str = "Unknown";
            switch (err) {
                case httplib::Error::Connection: err_str = "Connection"; break;
                case httplib::Error::ConnectionTimeout: err_str = "ConnectionTimeout"; break;
                case httplib::Error::Read: err_str = "Read"; break;
                case httplib::Error::Write: err_str = "Write"; break;
                case httplib::Error::SSLConnection: err_str = "SSLConnection"; break;
                case httplib::Error::Canceled: err_str = "Canceled"; break;
                case httplib::Error::ProxyConnection: err_str = "ProxyConnection"; break;
                default: break;
            }
            WARN("HTTP request failed: error={}({})", err_str, static_cast<int>(err));
            HandleApiError(std::format("HTTP error: {}({})", err_str, static_cast<int>(err)).c_str());
            return;
        }

        if (res->status == 200) {
            ParseResponse(res->body);
            std::lock_guard<std::mutex> lock(_error_mutex);
            _error_count = 0;
            _pause_phase = 0;
        } else if (res->status == 429 || res->status == 401 || res->status == 403) {
            String msg = std::format("TickFlow API error: status={}, body={}", res->status, res->body);
            WARN("{}", msg.c_str());
            HandleApiError(msg.c_str());
            return;
        } else {
            WARN("Unexpected status: {}", res->status);
            HandleApiError(std::format("HTTP {}", res->status).c_str());
            return;
        }

        // 批次间隔
        if (i + BATCH_SIZE < symbols.size()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

void TickFlowBridge::ParseResponse(const String& response) {
    try {
        auto json = nlohmann::json::parse(response);
        if (!json.contains("data") || !json["data"].is_array()) {
            WARN("Invalid response format");
            return;
        }

        int count = 0;
        for (auto& item : json["data"]) {
            QuoteInfo quote{};

            String code = item.value("symbol", "");
            quote._symbol = TickFlowToSymbol(code);

            quote._time = item.value("timestamp", 0);
            quote._open = item.value("open", 0.0);
            quote._close = item.value("last_price", 0.0);
            quote._high = item.value("high", 0.0);
            quote._low = item.value("low", 0.0);
            quote._volume = item.value("volume", 0);
            quote._turnover = item.value("amount", 0.0);

            // 涨跌停价：未知，填 0
            quote._upper = 0.0;
            quote._lower = 0.0;

            // 买卖 5 档：API 不提供，填充 0
            quote._bidPrice.fill(0);
            quote._bidVolume.fill(0);
            quote._askPrice.fill(0);
            quote._askVolume.fill(0);

            quote._source = 'T';  // TickFlow 来源标记
            quote._confidence = 100;

            _quotes.insert({quote._symbol, quote});
            count++;
        }
    } catch (const std::exception& e) {
        WARN("Parse error: {}", e.what());
    }
}

// ==================== 错误处理 ====================

void TickFlowBridge::HandleApiError(const String& reason) {
    std::lock_guard<std::mutex> lock(_error_mutex);
    _error_count++;

    // 3次→暂停1分钟 → 3次→暂停5分钟 → 3次→邮件通知并永久暂停
    if (_error_count >= 3) {
        if (_pause_phase == 0) {
            _pause_phase = 1;
            _paused = true;
            _pause_until = std::chrono::steady_clock::now() + std::chrono::minutes(1);
            WARN("3 consecutive errors, pausing for 1 minute. Reason: {}", reason.c_str());
        } else if (_pause_phase == 1) {
            _pause_phase = 2;
            _paused = true;
            _pause_until = std::chrono::steady_clock::now() + std::chrono::minutes(5);
            WARN("3 consecutive errors after resume, pausing for 5 minutes. Reason: {}", reason.c_str());
        } else if (_pause_phase == 2) {
            _pause_phase = 3;
            _paused = true;
            String msg = std::format("[TickFlowBridge] Bridge permanently paused due to persistent errors: {}", reason);
            FATAL("{}", msg.c_str());
            _server->SendEmail(msg.c_str());
        }

        _error_count = 0;
    }
}

// ==================== 合约信息查询 ====================

bool TickFlowBridge::GetAllStockSymbols(List<SymbolInfo>& symbols) {
    if (!_login_success) {
        WARN("TickFlowBridge not logged in");
        return false;
    }

    // 依次获取 SH、SZ、BJ 三大交易所
    static const Vector<String> exchanges = {"SH", "SZ", "BJ"};

    for (const auto& exch : exchanges) {
        size_t before = symbols.size();
        if (FetchStockSymbolsFromExchange(exch, symbols)) {
            INFO("Fetched {} stock symbols from exchange {}", symbols.size() - before, exch);
        } else {
            WARN("Failed to fetch stock symbols from exchange {}", exch);
        }
    }

    INFO("Total loaded {} stock symbols from TickFlow", symbols.size());
    return !symbols.empty();
}

bool TickFlowBridge::FetchStockSymbolsFromExchange(const String& exchangeCode, List<SymbolInfo>& symbols) {
    // 构建 URL: /v1/exchanges/{exchange}/instruments
    String url = std::format("/v1/exchanges/{}/instruments", exchangeCode);

    httplib::Headers headers = {
        {"x-api-key", _api_key}
    };

    auto res = _http_client->Get(url.c_str(), headers);

    if (!res) {
        auto err = res.error();
        const char* err_str = "Unknown";
        switch (err) {
            case httplib::Error::Connection: err_str = "Connection"; break;
            case httplib::Error::ConnectionTimeout: err_str = "ConnectionTimeout"; break;
            case httplib::Error::Read: err_str = "Read"; break;
            case httplib::Error::Write: err_str = "Write"; break;
            case httplib::Error::SSLConnection: err_str = "SSLConnection"; break;
            default: break;
        }
        WARN("HTTP request failed for exchange {}: error={}", exchangeCode, err_str);
        return false;
    }

    if (res->status != 200) {
        WARN("TickFlow API error for exchange {}: status={}, body={}", exchangeCode, res->status, res->body);
        return false;
    }

    try {
        auto json = nlohmann::json::parse(res->body);
        if (!json.contains("data") || !json["data"].is_array()) {
            WARN("Invalid response format for exchange {}", exchangeCode);
            return false;
        }

        // 交易所代码映射
        static const Map<String, ExchangeName> exchangeMap = {
            {"SH", MT_Shanghai},
            {"SZ", MT_Shenzhen},
            {"BJ", MT_Beijing}
        };

        auto exchange_it = exchangeMap.find(exchangeCode);
        ExchangeName exName = (exchange_it != exchangeMap.end()) ? exchange_it->second : MT_Unknow;

        for (const auto& item : json["data"]) {
            SymbolInfo info;
            info._code = item.value("code", "");
            info._name = item.value("name", "");
            info._exchange = exName;
            info._type = static_cast<char>(ContractType::AStock);

            // 过滤空代码
            if (info._code.empty()) {
                continue;
            }

            symbols.push_back(info);
        }

        return true;
    } catch (const std::exception& e) {
        WARN("Parse error for exchange {}: {}", exchangeCode, e.what());
        return false;
    }
}

SymbolInfo TickFlowBridge::GetSymbolInfo(const String& code) {
    SymbolInfo info;
    info._code = code;
    info._type = static_cast<char>(ContractType::AStock);

    // 根据代码推断交易所
    auto dotPos = code.rfind('.');
    if (dotPos != String::npos && dotPos + 2 <= code.size()) {
        String exch = code.substr(dotPos + 1);
        if (exch == "SH") info._exchange = MT_Shanghai;
        else if (exch == "SZ") info._exchange = MT_Shenzhen;
        else if (exch == "BJ") info._exchange = MT_Beijing;
    }

    return info;
}
