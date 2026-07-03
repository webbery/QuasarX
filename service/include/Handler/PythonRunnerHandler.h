#pragma once
#include "HttpHandler.h"

class PythonRunnerHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;
    void post(const httplib::Request& req, httplib::Response& res) override;
};
