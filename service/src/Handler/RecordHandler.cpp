#include "Handler/RecordHandler.h"
#include <algorithm>
#include <limits>
#include <nng/protocol/pubsub0/sub.h>
#include "Bridge/exchange.h"
#include "HttpHandler.h"
#include "Util/string_algorithm.h"
#include "server.h"
#include "json.hpp"

RecordHandler::RecordHandler(Server* server)
    : HttpHandler(server), _main(nullptr), _close(false) {
    _lastDuckDbFlush = std::chrono::steady_clock::now();
}

RecordHandler::~RecordHandler() {
    _close = true;
    if (_main) {
        _main->join();
        delete _main;
        _main = nullptr;
    }
    // 退出时 flush DuckDB 缓冲区
    FlushDuckDbBatch();
}

void RecordHandler::StartRecord(bool enable) {
    if (enable && !_main) {
        _close = false;
        _main = new std::thread(&RecordHandler::run, this);
    }
}

void RecordHandler::post(const httplib::Request &req, httplib::Response &res) {
    HandleReplayAction(req, res);
}

void RecordHandler::run() {
    if (!Subscribe(URI_RAW_QUOTE, sock)) {
        return;
    }
    SetCurrentThreadName("Recorder");
    _close = false;

    while (!_close) {
        char* buff = NULL;
        size_t sz;
        int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
        if (rv != 0) {
            nng_free(buff, sz);
            continue;
        }

        QuoteInfo quote;
        // 直接从二进制 buffer 反序列化（YAS 格式）
        constexpr std::size_t flags = yas::mem | yas::binary;
        yas::shared_buffer ybuf;
        ybuf.assign(buff, sz);
        yas::load<flags>(ybuf, quote);
        nng_free(buff, sz);

        // symbol 过滤：空集合 = 全记录，非空 = 只记录匹配的
        if (!_symbols.empty()) {
            String symStr = get_symbol(quote._symbol);
            if (symStr == "0" || _symbols.find(symStr) == _symbols.end()) {
                continue;
            }
        }

        // 推入 DuckDB 缓冲区
        PushDuckDbTick(quote);
    }
    nng_close(sock);
    sock.id = 0;

    printf("record stop.\n");
}

void RecordHandler::SetSymbols(const Set<String>& symbols) {
    _symbols = symbols;
}

// ============== DuckDB Tick 写入 ==============

void RecordHandler::PushDuckDbTick(const QuoteInfo& quote) {
    TickDataEntry entry;
    entry.id = 0;  // id 由 DuckDB IDENTITY 自动生成
    entry.timestamp_epoch = static_cast<int64_t>(quote._time);
    entry.symbol = format_symbol(get_symbol(quote._symbol));
    entry.open = quote._open;
    entry.close = quote._close;
    entry.high = quote._high;
    entry.low = quote._low;
    entry.volume = static_cast<int64_t>(quote._volume);
    entry.turnover = static_cast<int64_t>(quote._turnover);
    entry.value = quote._value;
    entry.upper = quote._upper;
    entry.lower = quote._lower;
    entry.source = std::string(1, quote._source);
    entry.confidence = static_cast<int>(quote._confidence);

    // 盘口：仅当 _bidPrice[0] > 0 时填充
    if (quote._bidPrice[0] > 0) {
        for (int i = 0; i < MAX_ORDER_SIZE_LVL2; ++i) {
            if (quote._bidPrice[i] > 0 || quote._bidVolume[i] > 0) {
                entry.bid_prices.push_back(quote._bidPrice[i]);
                entry.bid_volumes.push_back(static_cast<int64_t>(quote._bidVolume[i]));
                entry.ask_prices.push_back(quote._askPrice[i]);
                entry.ask_volumes.push_back(static_cast<int64_t>(quote._askVolume[i]));
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(_duckdbMutex);
        _duckdbBuffer.push_back(std::move(entry));
    }

    FlushDuckDbBatch();
}

void RecordHandler::FlushDuckDbBatch() {
    auto now = std::chrono::steady_clock::now();
    bool timeout = (now - _lastDuckDbFlush) >= std::chrono::seconds(5);

    Vector<TickDataEntry> batch;
    {
        std::lock_guard<std::mutex> lock(_duckdbMutex);
        if (_duckdbBuffer.empty() || (!timeout && _duckdbBuffer.size() < 500)) {
            return;
        }
        batch.swap(_duckdbBuffer);
    }

    // 推入 DuckDBLogger（id 由 DuckDB IDENTITY 自动生成）
    DuckDBLogger::instance().log_ticks(batch);
    _lastDuckDbFlush = now;
}

// ============== HTTP /replay 端点 ==============

void RecordHandler::HandleReplayAction(const httplib::Request &req, httplib::Response &res) {
    auto action = req.get_param_value("action");
    if (action.empty()) {
        action = "query";  // 默认行为：查询 tick
    }

    if (action == "query") {
        HandleTicksQuery(req, res);
    } else {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "unknown action: " + action}}.dump(), "application/json");
    }
}

void RecordHandler::HandleTicksQuery(const httplib::Request &req, httplib::Response &res) {
    String symbol = req.get_param_value("symbol");
    String startStr = req.get_param_value("start");
    String endStr = req.get_param_value("end");
    String limitStr = req.get_param_value("limit");

    int64_t start = startStr.empty() ? 0 : std::stoll(startStr);
    int64_t end = endStr.empty() ? INT64_MAX : std::stoll(endStr);
    int limit = limitStr.empty() ? 10000 : std::stoi(limitStr);
    limit = std::min(limit, 100000);  // 上限 10 万

    auto ticks = DuckDBLogger::instance().query_ticks(symbol, start, end, limit);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& tick : ticks) {
        nlohmann::json j;
        j["time"] = tick.timestamp_epoch;
        j["symbol"] = tick.symbol;
        j["open"] = tick.open;
        j["close"] = tick.close;
        j["high"] = tick.high;
        j["low"] = tick.low;
        j["volume"] = tick.volume;
        j["turnover"] = tick.turnover;
        j["value"] = tick.value;
        j["upper"] = tick.upper;
        j["lower"] = tick.lower;
        j["source"] = tick.source;
        j["confidence"] = tick.confidence;
        result.push_back(j);
    }

    res.set_content(result.dump(), "application/json");
}
