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
    }
    nng_close(sock);
    sock.id = 0;

    printf("record stop.\n");
}

void RecordHandler::SetSymbols(const Set<String>& symbols) {
    _symbols = symbols;
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
    if (symbol.empty()) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "missing required parameter: symbol"}}.dump(), "application/json");
        return;
    }

    // symbol 格式校验：必须包含 '.' 分隔符（内部格式 sz.800001 或外部格式 600000.SH）
    if (symbol.find('.') == String::npos) {
        res.status = 400;
        res.set_content(nlohmann::json{{"error", "invalid symbol format: " + symbol}}.dump(), "application/json");
        return;
    }

    String startStr = req.get_param_value("start");
    String endStr = req.get_param_value("end");
    String limitStr = req.get_param_value("limit");

    int64_t start = startStr.empty() ? 0 : std::stoll(startStr);
    int64_t end = endStr.empty() ? INT64_MAX : std::stoll(endStr);
    int limit = limitStr.empty() ? 10000 : std::stoi(limitStr);
    limit = std::min(limit, 100000);  // 上限 10 万

    // DuckDBLogger 已移除，返回空结果
    nlohmann::json result = nlohmann::json::array();

    res.set_content(result.dump(), "application/json");
}
