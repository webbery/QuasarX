#pragma once
#include "HttpHandler.h"
#include "Util/system.h"
#include "nng/nng.h"
#include <thread>
#include <mutex>

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
    void PushCborTick(const QuoteInfo& quote);
    void FlushCborBuffer();
    String CborFileName(const QuoteInfo& tick);

    // HTTP 查询接口
    void HandleTicksQuery(const httplib::Request &req, httplib::Response &res);

private:
    nng_socket sock;

    std::thread* _main;

    std::string _api;

    Set<String> _symbols;

    bool _close;

    // CBOR Tick 缓冲区
    struct CborBuffer {
        Vector<QuoteInfo> ticks;
        time_t lastFlush;
    };
    std::map<symbol_t, CborBuffer> _cborBuffers;
    String _cborBasePath;
};
