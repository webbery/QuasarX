#pragma once
#include "std_header.h"
#include "json.hpp"

/// 单笔成交记录（从基准回测提取）
struct CapacityTrade {
    symbol_t symbol;
    size_t day_index;         // 交易日索引（对齐后的 bar 序号）
    double price;             // 理想成交价（无冲击）
    int64_t shares;           // 成交股数
    uint8_t side;             // 0=买入, 1=卖出
    time_t time;              // 成交时间
};

/// 单资金量的扫描结果
struct CapacityPoint {
    double capital = 0;
    double sharpe = 0;
    double total_return = 0;
    double max_drawdown = 0;
    double win_rate = 0;
    double avg_participation = 0;
    double max_participation = 0;
    double avg_slippage_bps = 0;
    int orders_above_limit = 0;
    double sharpe_decay = 0;
};

/// 容量扫描摘要
struct CapacitySummary {
    double capacity_20pct = 0;
    double capacity_50pct = 0;
    String bottleneck_symbol;
    double bottleneck_adv = 0;
};

/// 策略容量扫描
///
/// 快速重放架构:
///   1. 外部运行基准回测（零冲击），提取交易日志
///   2. scan() 对每个资金量重放交易日志，计算冲击成本，重算指标
///   3. 信号不随资金量变化，只有仓位大小和冲击成本变化
class Capacity {
public:
    /// 快速重放扫描
    /// @param trades         基准回测交易日志（按时间排序）
    /// @param adv_data       每标的 ADV 时间序列
    /// @param vol_data       每标的日波动率时间序列
    /// @param base_capital   基准回测资金量
    /// @param min_capital    扫描最小资金
    /// @param max_capital    扫描最大资金
    /// @param steps          扫描步数（对数均匀）
    /// @param eta            冲击系数（平方根模型，A股默认1.0）
    /// @param max_participation 参与率上限
    Vector<CapacityPoint> scan(
        const Vector<CapacityTrade>& trades,
        const Map<symbol_t, Vector<double>>& adv_data,
        const Map<symbol_t, Vector<double>>& vol_data,
        double base_capital,
        double min_capital,
        double max_capital,
        int steps,
        double eta,
        double max_participation
    );

    /// 计算摘要
    static CapacitySummary computeSummary(
        const Vector<CapacityPoint>& curve,
        const Map<symbol_t, Vector<double>>& adv_data,
        double baseline_sharpe
    );

    /// 加载标的的 volume/close 数据，计算 ADV 和波动率
    static bool loadMarketData(
        const Vector<symbol_t>& symbols,
        int adv_window,
        Map<symbol_t, Vector<double>>& out_adv,
        Map<symbol_t, Vector<double>>& out_vol
    );

private:
    /// 平方根冲击模型
    static double computeSlippage(double sigma, double participation, double eta) {
        return eta * sigma * std::sqrt(std::max(participation, 0.0));
    }

    /// 单资金量快速重放
    static Vector<double> replayWithImpact(
        const Vector<CapacityTrade>& trades,
        const Map<symbol_t, Vector<double>>& adv_data,
        const Map<symbol_t, Vector<double>>& vol_data,
        double base_capital,
        double target_capital,
        double eta,
        size_t total_days,
        double max_participation,
        double& out_avg_part,
        double& out_max_part,
        double& out_avg_slippage_bps,
        int& out_orders_above
    );

    /// 从日收益率计算指标
    static void computeMetrics(const Vector<double>& daily_returns, CapacityPoint& point);
};
