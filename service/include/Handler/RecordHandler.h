#pragma once
#include "HttpHandler.h"
#include "Util/system.h"
#include "nng/nng.h"
#include <thread>
#include <mutex>
#include <chrono>
#include "Bridge/exchange.h"
#include "Util/DuckDBLogger.h"

class Server;
class RecordHandler: public HttpHandler {
public:
    RecordHandler(Server* server);
    ~RecordHandler();

    void run();
    void StartRecord(bool enable);
    void SetSymbols(const Set<String>& symbols);

    virtual void post(const httplib::Request &req, httplib::Response &res);

private:
    void HandleTicksQuery(const httplib::Request &req, httplib::Response &res);
    void HandleReplayAction(const httplib::Request &req, httplib::Response &res);

    // DuckDB Tick 写入
    void PushDuckDbTick(const QuoteInfo& quote);
    void FlushDuckDbBatch();

private:
    nng_socket sock;
    std::thread* _main;
    Set<String> _symbols;
    bool _close;

    // DuckDB 异步写入缓冲区
    Vector<TickDataEntry> _duckdbBuffer;
    std::mutex _duckdbMutex;
    std::chrono::steady_clock::time_point _lastDuckDbFlush;
};
