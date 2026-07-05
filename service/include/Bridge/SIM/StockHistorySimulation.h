#pragma once
#include "Bridge/SIM/HistorySimulationBase.h"
#include "Bridge/SlippageModel.h"

#define STOCK_HISTORY_SIM "stock_hist_sim"

/**
 * @brief 股票历史数据回测（DuckDB 数据源）
 *
 * 继承 HistorySimulationBase，实现股票特有逻辑：
 * - 数据源: DuckDB 表 stock_1d / stock_1m / stock_5m
 * - T+0/T+1 由 UseLevel() 决定频率
 * - 股票佣金/印花税默认配置
 */
class StockHistorySimulation : public HistorySimulationBase {
public:
    StockHistorySimulation(Server* server);
    ~StockHistorySimulation();

    virtual const char* Name() override { return STOCK_HISTORY_SIM; }

    bool Init(const ExchangeInfo& handle) override;

    virtual bool GetPosition(AccountPosition&) override;
    virtual AccountAsset GetAsset() override;

    /// @brief 设置交易模式 (T0/T1)
    void UseLevel(TradingMode mode) { _tradingMode = mode; }

    /// @brief 获取当前交易模式
    TradingMode GetTradingMode() const { return _tradingMode; }

    /// @brief 设置 T0 数据频率 (1m/5m/15m)，仅分钟级回测使用
    void SetT0Freq(const String& freq) { _t0Freq = freq; }

    /// @brief 获取后复权收盘价和 datetime 序列
    std::pair<std::vector<time_t>, std::vector<double>> GetHFQCloseData(symbol_t symbol) const override;

protected:
    bool LoadData(const String& code) override;
    std::pair<Commission, Commission> GetDefaultCommission() const override;
    void OnDataLoaded() override;

private:
    TradingMode _tradingMode;  // 交易模式（T0/T1，决定数据加载方式）
    String _t0Freq;            // T0 数据频率（1m/5m/15m），仅分钟级使用
};
