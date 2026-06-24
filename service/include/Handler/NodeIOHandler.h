#pragma once
#include "HttpHandler.h"

class NodeIOHandler: public HttpHandler {
public:
    NodeIOHandler(Server* server): HttpHandler(server) {}
    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void del(const httplib::Request& req, httplib::Response& res);

private:
    void query_default(const httplib::Request& req, httplib::Response& res);
    void cleanup_old(const httplib::Request& req, httplib::Response& res);
    std::string get_param(const httplib::Request& req, const std::string& name);
    int64_t get_int64_param(const httplib::Request& req, const std::string& name, int64_t default_val);
    int get_int_param(const httplib::Request& req, const std::string& name, int default_val);
};
