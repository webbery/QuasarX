#pragma once
#include "HttpHandler.h"

class Server;
class FeatureHandler: public HttpHandler {
public:
    FeatureHandler(Server* );
    virtual void get(const httplib::Request& req, httplib::Response& res);
};