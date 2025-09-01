#pragma once
#include "HttpHandler.h"
#include "Util/system.h"
#include "nng/nng.h"
#include <fstream>
#include <thread>
#include "Bridge/XTP/XTPExchange.h"

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
    void WriteCSV(std::fstream& fs, const QuoteInfo& infos);

    std::fstream& GetFileStream(const symbol_t& name);

    String GetTypePath(symbol_t sym);
    // 合并备份数据
    bool MergeBackup(const String& src, const String& dst);

    bool MergeCSV(const String& src, const String& dst);
private:
    nng_socket sock;

    std::thread* _main;

    std::string _api;

    Set<String> _symbols;

    std::map<symbol_t, std::fstream> _fs_map;
    bool _close;
};
