#pragma once

#include "HttpHandler.h"
#include "json.hpp"

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

class SystemConfigHandler: public HttpHandler {
public:
    SystemConfigHandler(Server*);
    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void post(const httplib::Request& req, httplib::Response& res);

private:
    bool AddExchange(nlohmann::json& config, const nlohmann::json& params);
    bool DeleteExchange(nlohmann::json& config, const String& name);
    bool ChangePassword(nlohmann::json& config, const String& org, const String& latest);
    bool ChangeCommission(nlohmann::json& config, const nlohmann::json& params);
    bool ChangeSMTP(nlohmann::json& config, const nlohmann::json& params);
    bool ChangeSchedule(nlohmann::json& config, const nlohmann::json& params);
    // input:09:30-11:30;13:00-15:00
    List<String> FormatActiveTime(const nlohmann::json& times);
};
