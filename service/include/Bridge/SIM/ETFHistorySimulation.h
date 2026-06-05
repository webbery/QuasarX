#pragma once
#include "Bridge/SIM/HistorySimulationBase.h"

#define ETF_HISTORY_SIM "etf_hist_sim"

/**
 * @brief ETF 历史数据回测
 *
 * 支持分钟级 ETF 数据回测：
 * - 后复权价格: etf_hfq/{freq}/{code}.csv
 * - 原始价格:   etf_org/{freq}/{code}.csv
 * - 佣金: 万 0.5，无最低限制
 * - 无印花税
 * - T+0/T+1 由 config.json 的 etf.t0/etf.t1 配置决定
 */
class ETFHistorySimulation : public HistorySimulationBase {
public:
    ETFHistorySimulation(Server* server);

    virtual const char* Name() override { return ETF_HISTORY_SIM; }

    bool Init(const ExchangeInfo& handle) override;

    /// @brief 设置 ETF 代码列表和交易模式
    void SetEtfCodes(const Set<String>& t0Codes, const Set<String>& t1Codes);

    /// @brief 设置数据频率 (1m/5m/15m)
    void UseFreq(const String& freq) { _freq = freq; }

protected:
    bool LoadData(const String& code) override;
    std::pair<Commission, Commission> GetDefaultCommission() const override;
    void OnDataLoaded() override;

private:
    String _freq = "1m";  // 1m/5m/15m
    Set<String> _t0Codes; // T+0 ETF 代码列表
    Set<String> _t1Codes; // T+1 ETF 代码列表

    /// @brief 判断 ETF 代码是 T+0 还是 T+1
    bool IsT0(const String& code) const;
};
