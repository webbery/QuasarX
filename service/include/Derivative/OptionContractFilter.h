#pragma once
#include "std_header.h"
#include "Derivative/OptionPricer.h"
#include "json.hpp"

// 期权合约异常报价过滤器
// 用于 IV 曲面构建前剔除流动性差、价差异常、moneyness 极端、parity 违反的合约
//
// 6 层过滤 (由硬到软):
//   L1 硬过滤:  iv 越界 / strike<=0 / close<=0 / expiry<1 / 深度价内
//   L2 流动性:  volume==0 且 oi<10, 或末日低成交额
//   L3 价差代理: (high-low)/mid > 阈值 (无 bid/ask 字段时的退化处理)
//   L4 moneyness: K/S 超出产品默认范围
//   L5 parity: 同 strike 同 expiry 的 call/put 违反 put-call parity
//   L6 修复: 同 strike call/put 的 IV 不再简单平均 (在 IVSurface 层处理)

struct OptionContractView {
    String contract_name;
    OptionType opt_type = OptionType::Unknown;
    double strike;
    double close;             // 收盘价
    double settlement;        // 结算价 (可为 0)
    double high;              // 日内最高
    double low;               // 日内最低
    double open;              // 日内开盘
    int64_t volume;           // 成交量
    int64_t turnover;         // 成交额 (元)
    int64_t open_interest;    // 持仓量
    double iv;                // 交易所公布 IV (反算后填入)
    int expiry_days;          // 到期天数
    double spot;              // 标的现货价
    double risk_free_rate;    // 无风险利率
};

struct FilterConfig {
    // L1 硬过滤
    double iv_min = 0.05;
    double iv_max = 3.0;

    // L2 流动性
    int64_t oi_min_when_no_volume = 10;
    int64_t turnover_min_short_expiry = 1000;
    int short_expiry_days_threshold = 14;

    // L3 价差代理
    double spread_proxy_max = 0.5;   // (high-low)/mid

    // L4 moneyness
    double moneyness_min_etf = 0.7;
    double moneyness_max_etf = 1.3;
    double moneyness_min_index = 0.85;
    double moneyness_max_index = 1.15;

    // L5 parity
    double parity_tolerance = 0.05;  // 元

    // 产品类别 (影响 moneyness 范围)
    bool is_index_option = false;    // true: CFFEX 股指, false: SSE/SZSE ETF
};

struct FilterStats {
    int total_contracts = 0;
    int filtered_count = 0;
    int removed_L1_hard = 0;
    int removed_L2_liquidity = 0;
    int removed_L3_spread_proxy = 0;
    int removed_L4_moneyness = 0;
    int removed_L5_parity = 0;

    struct RemovedEntry {
        String contract_name;
        String layer;
        String reason;
    };
    Vector<RemovedEntry> removed_contracts;

    nlohmann::json toJson() const;
};

// 过滤结果: 通过的合约 + 统计
struct FilterResult {
    Vector<OptionContractView> kept;
    FilterStats stats;
};

class OptionContractFilter {
public:
    explicit OptionContractFilter(FilterConfig cfg = {}) : _cfg(std::move(cfg)) {}

    // 对一组合约执行 6 层过滤
    FilterResult apply(Vector<OptionContractView> contracts) const;

private:
    FilterConfig _cfg;

    // 各层过滤,返回 true 表示通过
    bool passL1(const OptionContractView& c, String& reason) const;
    bool passL2(const OptionContractView& c, String& reason) const;
    bool passL3(const OptionContractView& c, String& reason) const;
    bool passL4(const OptionContractView& c, String& reason) const;

    // L5: 跨 call/put 的 parity 检查 (需要整组数据)
    void applyL5(Vector<OptionContractView>& contracts, FilterStats& stats) const;
};
