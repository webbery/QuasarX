#include "Bridge/TickFlow/TickFlowBridge.h"
#include "Bridge/SIM/BacktestContext.h"
#include "ExchangeManager.h"
#include "Util/finance.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "json.hpp"
#include "server.h"
#include <algorithm>
#include <format>
#include <deque>
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/x509.h>
#endif

TickFlowBridge::TickFlowBridge(Server* server)
    : ExchangeInterface(server),
      _base_url("https://api.tickflow.org"),
      _universes{"CN_Equity_A"},
      _interval_ms(10000),
      _login_success(false),
      _error_count(0),
      _pause_phase(0),
      _paused(false),
      _workerThread(nullptr),
      _stop(false) {
}

TickFlowBridge::~TickFlowBridge() {
    _stop = true;
    if (_workerThread && _workerThread->joinable()) {
        _workerThread->join();
    }
    delete _workerThread;
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
    _interval_ms = 10000;

    InitHttpClient();

    // 启动自治调度线程（nanomsg socket 的 init/use/destroy 都在此线程内）
    _workerThread = new std::thread(&TickFlowBridge::workerLoop, this);

    // 等待 worker 线程就绪（避免竞态：主线程在 RegisterExchange 中调用 GetAllStockSymbols 时 _login_success 尚未被设置）
    // workerLoop 开头会设置 _login_success = true，这里最多等 1s
    for (int i = 0; i < 200 && !_login_success; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!_login_success) {
        WARN("TickFlowBridge worker thread failed to initialize after 1s timeout");
        return false;
    }

    INFO("TickFlowBridge initialized, api_key loaded, worker thread started");
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

    // TickFlow 限频 10次/分钟，留有余量，10s 一次请求
    // worker 线程已按 _interval_ms 控制频率
    _interval_ms = 10000;

    INFO("SetFilter: {} symbols, interval={}ms", filter._symbols.size(), _interval_ms);
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
    if (_capitalPool && !_strategyName.empty()) {
        return _capitalPool->getAvailable(_strategyName);
    }
    return 0.0;
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

    // 更新模拟持仓并从 CapitalPool 扣/加资金
    double price = order->_order._price;
    int64_t qty = order->_order._volume;
    if (order->_order._side == 0) {
        TradeFees fees = _positionMgr.Buy(symbol, qty, price);
        if (_capitalPool && !_strategyName.empty()) {
            double amount = qty * price;
            _capitalPool->updateAvailable(_strategyName, -(amount + fees.total()));
        }
    } else {
        auto result = _positionMgr.Sell(symbol, qty, price);
        if (_capitalPool && !_strategyName.empty()) {
            _capitalPool->updateAvailable(_strategyName, result.proceeds);
        }
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

    // 发送订单邮件通知（含流动性指标）
    SendOrderEmail(symbol, report);

    // 触发基类回调（持仓更新等）
    OnOrderReport({}, report);

    // 标记订单成功
    order->_flag = true;
    order->_success = true;
    try {
        order->_promise.set_value(true);
    } catch (...) {}

    return order_id{};
}

void TickFlowBridge::SendOrderEmail(symbol_t symbol, const TradeReport& report) {
    if (!_server) return;

    String symbol_str = SymbolToTickFlow(symbol);

    // 构建基础订单信息
    String content;
    if (report._side == 0) {
        content = std::format("Buy {} @ {:.4f}  volume={}\n",
                             symbol_str, report._price, report._quantity);
    } else {
        content = std::format("Sell {} @ {:.4f}  volume={}\n",
                             symbol_str, report._price, report._quantity);
    }

    // 计算流动性指标
    std::lock_guard<std::mutex> lock(_tickHistoryMutex);
    auto it = _tickHistory.find(symbol);
    if (it != _tickHistory.end() && it->second.size() >= 2) {
        const auto& history = it->second;
        Vector<double> prices;
        Vector<int64_t> volumes;
        prices.reserve(history.size());
        volumes.reserve(history.size());
        for (const auto& tick : history) {
            prices.push_back(tick.price);
            volumes.push_back(tick.volume);
        }

        double kyle = finance::kyles_lambda(prices, volumes, report._side, report._quantity);
        double amihud = finance::amihud_illiquidity(prices, volumes);

        content += "\n--- Liquidity Metrics ---\n";
        content += std::format("Kyle's Lambda:    {:.6e}\n", kyle);
        content += std::format("Amihud Illiquid:  {:.6e}\n", amihud);

        // 流动性评级（基于 Kyle's Lambda）
        if (kyle < 1e-7) {
            content += "Liquidity Level:  HIGH (Liquid)\n";
        } else if (kyle < 1e-6) {
            content += "Liquidity Level:  MEDIUM\n";
        } else if (kyle < 1e-5) {
            content += "Liquidity Level:  LOW (Illiquid)\n";
        } else {
            content += "Liquidity Level:  VERY LOW (Highly Illiquid)\n";
        }

        content += std::format("Tick Window:    {} bars\n", history.size());
    } else {
        content += "\n--- Liquidity Metrics ---\n";
        content += "Insufficient tick data for calculation\n";
    }

    if (!content.empty()) {
        _server->SendEmail(content);
    }
}

void TickFlowBridge::OnOrderReport(order_id id, const TradeReport& report) {
    // 基类回调：由 BrokerSubSystem 等调用，此处保留空实现
    // 邮件通知已移至 AddOrder 中的 SendOrderEmail
}

// ==================== 自治调度线程 ====================

/**
 * workerLoop: 自治调度线程
 * - 按 _interval_ms 周期轮询 TickFlow API
 * - 检查工作时间 + 暂停状态
 * - 行情发布通过 ExchangeManager::QueueToPublish 统一到 dispatch 线程发送
 */
void TickFlowBridge::workerLoop() {
    using Clock = std::chrono::steady_clock;

    // 行情发布已统一到 ExchangeManager 的 dispatch 线程，本线程不再创建 pub socket
    _login_success = true;

    auto nextWakeup = Clock::now();

    while (!_stop) {
        // 检查当前时间
        time_t curr = _server ? Now() : std::time(nullptr);

        // 检查工作时间
        if (!IsWorking(curr)) {
            // 非工作时间，暂停 30s 再检查
            std::this_thread::sleep_for(std::chrono::seconds(30));
            continue;
        }

        // 检查暂停状态
        {
            std::lock_guard<std::mutex> lock(_error_mutex);
            if (_paused) {
                auto now = Clock::now();
                if (now < _pause_until) {
                    long remaining = std::chrono::duration_cast<std::chrono::seconds>(_pause_until - now).count();
                    std::this_thread::sleep_for(std::chrono::seconds(std::min(remaining, 5L)));
                    continue;
                } else {
                    _paused = false;
                    _error_count = 0;
                    INFO("[TickFlow] Pause expired, resuming");
                }
            }
        }

        // 检查频率控制
        {
            std::lock_guard<std::mutex> lock(_error_mutex);
            auto now = Clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - _last_request).count();
            auto wait = _interval_ms - elapsed;
            if (wait > 0) {
                // 等待到下一个请求窗口，但每 1s 检查一次 _stop
                auto deadline = Clock::now() + std::chrono::milliseconds(wait);
                while (Clock::now() < deadline && !_stop) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(std::min(1000L,
                        (long)std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count())));
                }
                if (_stop) break;
            }
            _last_request = Clock::now();
        }

        // 检查标的列表
        if (_filter._symbols.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        // 执行 HTTP 请求 + 发布行情（同线程内 nanomsg 安全）
        FetchQuotes();

        // 下一轮唤醒
        nextWakeup += std::chrono::milliseconds(_interval_ms);
        auto sleepDur = nextWakeup - Clock::now();
        if (sleepDur.count() > 0) {
            std::this_thread::sleep_for(sleepDur);
        } else {
            nextWakeup = Clock::now() + std::chrono::milliseconds(_interval_ms);
        }
    }

    INFO("[TickFlow] worker: thread stopped");
}

// ==================== 行情 ====================

QuoteInfo TickFlowBridge::GetQuote(symbol_t symbol) {
    QuoteInfo quote{};
    _quotes.visit(symbol, [&quote](std::pair<const symbol_t, QuoteInfo>& entry) {
        quote = entry.second;
    });
    return quote;
}

void TickFlowBridge::PublishQuote(const QuoteInfo& quote) {
    if (!_server) return;
    auto* exchMgr = _server->GetExchangeManager();
    if (!exchMgr) return;
    exchMgr->QueueToPublish(quote);
}

// ==================== HTTP 请求 ====================

void TickFlowBridge::FetchQuotes() {
    if (!_login_success || _filter._symbols.empty()) return;

    auto symbols = Vector<String>(_filter._symbols.begin(), _filter._symbols.end());

    // _offset 到达末尾后重置，每轮请求只发送一个批次
    if (_offset >= static_cast<int>(symbols.size())) {
        _offset = 0;
    }

    constexpr int BATCH_SIZE = 5;
    int upper = std::min(static_cast<int>(symbols.size()), _offset + BATCH_SIZE);
    int offset = upper - _offset;
    if (offset <= 0) return;

    Vector<String> batch(symbols.begin() + _offset, symbols.begin() + upper);
    _offset = upper;

    // 构建请求体
    nlohmann::json body;
    body["symbols"] = batch;

    String body_str = body.dump();

    // 发送 POST 请求
    httplib::Headers headers = {
        {"Content-Type", "application/json"},
        { "x-api-key", _api_key }
    };

    DEBUG_INFO("[TickFlow] POST /v1/quotes | key_prefix={} | symbols={}",
                _api_key.empty() ? "(empty)" : _api_key.substr(0, 8), body_str);

    auto res = _http_client->Post("/v1/quotes", headers, body_str.c_str(), body_str.size(), "application/json");

    if (!res) {
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

    DEBUG_INFO("[TickFlow] Response: status={} body_len={}", res->status, res->body.size());
    if (res->status != 200) {
        DEBUG_INFO("[TickFlow] Response body: {}", res->body);
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

}

void TickFlowBridge::ParseResponse(const String& response) {
    try {
        auto json = nlohmann::json::parse(response);
        if (!json.contains("data") || !json["data"].is_array()) {
            WARN("Invalid response format");
            return;
        }

        int count = 0;
        int published = 0;
        for (auto& item : json["data"]) {
            QuoteInfo quote{};

            String code = item.value("symbol", "");
            quote._symbol = TickFlowToSymbol(code);
            // INFO("quote {}", item.dump());
            quote._time = (size_t)item["timestamp"]/1000;
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

            // 记录tick历史（用于流动性计算）
            {
                std::lock_guard<std::mutex> lock(_tickHistoryMutex);
                auto& history = _tickHistory[quote._symbol];
                history.push_back(TickRecord{quote._time, quote._close, quote._volume});
                // 保留最近 LIQUIDITY_WINDOW + 1 个tick
                if (history.size() > LIQUIDITY_WINDOW + 1) {
                    history.pop_front();
                }
            }

            // 发布到 URI_RAW_QUOTE，供 RecordHandler 等订阅者使用
            PublishQuote(quote);
            published++;
        }
        DEBUG_INFO("[TickFlow] Parsed {} quotes, published {} to URI_RAW_QUOTE", count, published);
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
