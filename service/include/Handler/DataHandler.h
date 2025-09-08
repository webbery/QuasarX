#pragma once

#include "HttpHandler.h"

class DataSyncHandler: public HttpHandler {
public:
    DataSyncHandler(Server* );
    virtual void get(const httplib::Request& req, httplib::Response& res);

private:
    void SendFile(const String& filepath, httplib::Response& res);

    bool CreateZip(const String& dirs, const String& dstfile);
};