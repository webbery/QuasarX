#pragma once
#include "HttpHandler.h"

class QuoteDownloadHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void get(const httplib::Request& req, httplib::Response& res) override;
};
