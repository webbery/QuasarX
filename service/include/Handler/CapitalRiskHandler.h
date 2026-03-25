#pragma once
#include "HttpHandler.h"
#include "Risk/CapitalRiskManager.h"

class Server;

/**
 * 总资金风控 Handler
 * GET  /v0/risk/capital - 获取总资金风控配置和状态
 * POST /v0/risk/capital - 设置总资金止损配置
 */
class CapitalRiskHandler : public HttpHandler {
public:
    CapitalRiskHandler(Server* server);
    ~CapitalRiskHandler();

    virtual void get(const httplib::Request& req, httplib::Response& res) override;
    virtual void post(const httplib::Request& req, httplib::Response& res) override;

    /**
     * 获取 CapitalRiskManager 实例
     */
    CapitalRiskManager* GetCapitalRiskManager() { return _riskManager; }

    /**
     * 设置 CapitalRiskManager 实例
     */
    void SetCapitalRiskManager(CapitalRiskManager* manager) { _riskManager = manager; }

private:
    CapitalRiskManager* _riskManager;
};

/**
 * 单日亏损风控 Handler
 * GET  /v0/risk/daily - 获取单日亏损风控配置和状态
 * POST /v0/risk/daily - 设置单日亏损限额配置
 */
class DailyLossRiskHandler : public HttpHandler {
public:
    DailyLossRiskHandler(Server* server);
    ~DailyLossRiskHandler();

    virtual void get(const httplib::Request& req, httplib::Response& res) override;
    virtual void post(const httplib::Request& req, httplib::Response& res) override;

    /**
     * 获取 CapitalRiskManager 实例
     */
    CapitalRiskManager* GetCapitalRiskManager() { return _riskManager; }

    /**
     * 设置 CapitalRiskManager 实例
     */
    void SetCapitalRiskManager(CapitalRiskManager* manager) { _riskManager = manager; }

private:
    CapitalRiskManager* _riskManager;
};

/**
 * 全部平仓 Handler
 * POST /v0/risk/closeall - 手动触发全部平仓
 */
class CloseAllPositionHandler : public HttpHandler {
public:
    CloseAllPositionHandler(Server* server);
    ~CloseAllPositionHandler();

    virtual void post(const httplib::Request& req, httplib::Response& res) override;

    /**
     * 获取 CapitalRiskManager 实例
     */
    CapitalRiskManager* GetCapitalRiskManager() { return _riskManager; }

    /**
     * 设置 CapitalRiskManager 实例
     */
    void SetCapitalRiskManager(CapitalRiskManager* manager) { _riskManager = manager; }

private:
    CapitalRiskManager* _riskManager;
};
