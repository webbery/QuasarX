#pragma once

#include "HttpHandler.h"

class UserLoginHandler: public HttpHandler {
public:
    UserLoginHandler(Server*);
    virtual void post(const httplib::Request& req, httplib::Response& res);

private:
    
};

class ServerStatusHandler: public HttpHandler {
public:
    ServerStatusHandler(Server*);
    virtual void post(const httplib::Request& req, httplib::Response& res);
    virtual void get(const httplib::Request& req, httplib::Response& res);

private:
    std::vector<unsigned long long> readCPUStats();
    double getCPUUsage();

    double getMemoryInfo();

private:
    std::vector<unsigned long long> _previousCPUStats;
};