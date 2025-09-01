#pragma once

#include "HttpHandler.h"

class DataHandler: public HttpHandler {
public:
    DataHandler(Server* );
    virtual void get(const httplib::Request& req, httplib::Response& res);

};