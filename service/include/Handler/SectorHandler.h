#pragma once
#include "HttpHandler.h"

class SectorHandler: public HttpHandler {
public:
    SectorHandler(Server*);
    
    virtual void get(const httplib::Request& req, httplib::Response& res);
};