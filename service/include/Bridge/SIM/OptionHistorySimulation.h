#pragma once
#include "Bridge/SIM/HistorySimulationBase.h"

#define OPTION_HISTORY_SIM "option_hist_sim"

/**
 * @brief 期权历史数据回测（DuckDB OptionDataDB 数据源）
 *
 * 支持日级 ETF 期权回测：
 * - 数据源: OptionDataDB (option_daily 表)
 * - 佣金: 每张 3 元（默认）
 * - 无印花税
 * - T+0 交易
 */
class OptionHistorySimulation : public HistorySimulationBase {
public:
    OptionHistorySimulation(Server* server);

    virtual const char* Name() override { return OPTION_HISTORY_SIM; }

    bool Init(const ExchangeInfo& handle) override;

protected:
    bool LoadData(const String& code) override;
    std::pair<Commission, Commission> GetDefaultCommission() const override;
    void OnDataLoaded() override;
};
