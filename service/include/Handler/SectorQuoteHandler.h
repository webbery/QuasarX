#pragma once
#include "HttpHandler.h"

class SectorQuoteHandler: public HttpHandler {
public:
    SectorQuoteHandler(Server*);

    virtual void get(const httplib::Request& req, httplib::Response& res);
};
