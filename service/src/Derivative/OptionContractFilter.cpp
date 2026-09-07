#include "Derivative/OptionContractFilter.h"
#include <algorithm>
#include <cmath>
#include <map>

// ═══════════════════════════════════════════════════════════════════
//  FilterStats::toJson
// ═══════════════════════════════════════════════════════════════════

nlohmann::json FilterStats::toJson() const {
    nlohmann::json j;
    j["total_contracts"] = total_contracts;
    j["filtered_count"] = filtered_count;
    j["removed_by_layer"] = {
        {"L1_hard", removed_L1_hard},
        {"L2_liquidity", removed_L2_liquidity},
        {"L3_spread_proxy", removed_L3_spread_proxy},
        {"L4_moneyness", removed_L4_moneyness},
        {"L5_parity", removed_L5_parity}
    };
    nlohmann::json removed_arr = nlohmann::json::array();
    for (auto& e : removed_contracts) {
        removed_arr.push_back({
            {"contract_name", e.contract_name},
            {"layer", e.layer},
            {"reason", e.reason}
        });
    }
    j["removed_contracts"] = std::move(removed_arr);
    return j;
}

// ═══════════════════════════════════════════════════════════════════
//  OptionContractFilter::apply
// ═══════════════════════════════════════════════════════════════════

FilterResult OptionContractFilter::apply(Vector<OptionContractView> contracts) const {
    FilterResult result;
    result.stats.total_contracts = static_cast<int>(contracts.size());

    // L1~L4: 逐条过滤
    for (auto& c : contracts) {
        String reason;

        if (!passL1(c, reason)) {
            result.stats.removed_L1_hard++;
            result.stats.removed_contracts.push_back({c.contract_name, "L1", reason});
            continue;
        }

        if (!passL2(c, reason)) {
            result.stats.removed_L2_liquidity++;
            result.stats.removed_contracts.push_back({c.contract_name, "L2", reason});
            continue;
        }

        if (!passL3(c, reason)) {
            result.stats.removed_L3_spread_proxy++;
            result.stats.removed_contracts.push_back({c.contract_name, "L3", reason});
            continue;
        }

        if (!passL4(c, reason)) {
            result.stats.removed_L4_moneyness++;
            result.stats.removed_contracts.push_back({c.contract_name, "L4", reason});
            continue;
        }

        result.kept.push_back(std::move(c));
    }

    // L5: 跨 call/put 的 parity 检查 (需要整组数据)
    applyL5(result.kept, result.stats);

    result.stats.filtered_count = static_cast<int>(result.kept.size());
    return result;
}

// ═══════════════════════════════════════════════════════════════════
//  L1 硬过滤
// ═══════════════════════════════════════════════════════════════════

bool OptionContractFilter::passL1(const OptionContractView& c, String& reason) const {
    if (c.strike <= 0) {
        reason = "strike<=0";
        return false;
    }
    if (c.close <= 0) {
        reason = "close<=0";
        return false;
    }
    if (c.expiry_days < 1) {
        reason = "expiry_days<1";
        return false;
    }
    if (c.iv < _cfg.iv_min) {
        reason = fmt::format("iv={}<{:.2f}", c.iv, _cfg.iv_min);
        return false;
    }
    if (c.iv > _cfg.iv_max) {
        reason = fmt::format("iv={}>{:.1f}", c.iv, _cfg.iv_max);
        return false;
    }
    // 深度价内: close < intrinsic - 1e-8 (仅当 spot>0 时检查;
    // spot<=0 时上游 IV surface handler 未取到标的行情, intrinsic 退化为 strike/0,
    // 强行计算会误杀 PUT, 应跳过此检查把责任放回给上游)
    if (c.spot > 0) {
        double intrinsic = (c.opt_type == OptionType::Call)
            ? std::max(c.spot - c.strike, 0.0)
            : std::max(c.strike - c.spot, 0.0);
        if (c.close < intrinsic - 1e-8) {
            reason = fmt::format("close={:.4f}<intrinsic={:.4f}", c.close, intrinsic);
            return false;
        }
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════
//  L2 流动性过滤
// ═══════════════════════════════════════════════════════════════════

bool OptionContractFilter::passL2(const OptionContractView& c, String& reason) const {
    // 当日无成交 且 历史持仓极低
    if (c.volume == 0 && c.open_interest < _cfg.oi_min_when_no_volume) {
        reason = fmt::format("volume=0, oi={}<{}", c.open_interest, _cfg.oi_min_when_no_volume);
        return false;
    }
    // 末日低成交额
    if (c.expiry_days < _cfg.short_expiry_days_threshold &&
        c.turnover < _cfg.turnover_min_short_expiry) {
        reason = fmt::format("expiry<{}d, turnover={}<{}",
                             _cfg.short_expiry_days_threshold,
                             c.turnover,
                             _cfg.turnover_min_short_expiry);
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════
//  L3 价差代理 (用 high-low 估算 bid-ask spread)
// ═══════════════════════════════════════════════════════════════════

bool OptionContractFilter::passL3(const OptionContractView& c, String& reason) const {
    if (c.high <= 0 || c.low <= 0 || c.close <= 0) {
        // 数据缺失时跳过此检查
        return true;
    }
    double mid = (c.high + c.low + c.close) / 3.0;
    if (mid <= 0) return true;
    double spread_proxy = (c.high - c.low) / mid;
    if (spread_proxy > _cfg.spread_proxy_max) {
        reason = fmt::format("(high-low)/mid={:.3f}>{:.2f}", spread_proxy, _cfg.spread_proxy_max);
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════
//  L4 moneyness 过滤
// ═══════════════════════════════════════════════════════════════════

bool OptionContractFilter::passL4(const OptionContractView& c, String& reason) const {
    if (c.spot <= 0) return true;
    double moneyness = c.strike / c.spot;
    double min_m, max_m;
    if (_cfg.is_index_option) {
        min_m = _cfg.moneyness_min_index;
        max_m = _cfg.moneyness_max_index;
    } else {
        min_m = _cfg.moneyness_min_etf;
        max_m = _cfg.moneyness_max_etf;
    }
    if (moneyness < min_m || moneyness > max_m) {
        reason = fmt::format("K/S={:.3f} not in [{:.2f}, {:.2f}]", moneyness, min_m, max_m);
        return false;
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════
//  L5 Put-Call Parity 检查
// ═══════════════════════════════════════════════════════════════════

void OptionContractFilter::applyL5(Vector<OptionContractView>& contracts, FilterStats& stats) const {
    // 按 (strike, expiry_days) 分组
    std::map<std::pair<double, int>, Vector<size_t>> groups;
    for (size_t i = 0; i < contracts.size(); ++i) {
        auto& c = contracts[i];
        groups[{c.strike, c.expiry_days}].push_back(i);
    }

    std::set<size_t> to_remove;

    for (auto& [key, indices] : groups) {
        // 找 call 和 put
        int call_idx = -1, put_idx = -1;
        for (size_t idx : indices) {
            if (contracts[idx].opt_type == OptionType::Call) call_idx = static_cast<int>(idx);
            else if (contracts[idx].opt_type == OptionType::Put) put_idx = static_cast<int>(idx);
        }

        if (call_idx < 0 || put_idx < 0) continue;  // 单边合约跳过

        auto& call_c = contracts[call_idx];
        auto& put_c = contracts[put_idx];

        // 理论价差: C - P = S - K * e^(-rT)
        double T = std::max(call_c.expiry_days, 1) / 365.0;
        double theoretical = call_c.spot - call_c.strike * std::exp(-call_c.risk_free_rate * T);
        double actual = call_c.close - put_c.close;
        double deviation = std::abs(actual - theoretical);

        if (deviation > _cfg.parity_tolerance) {
            // 优先剔除 iv 偏离中位数更大的那个
            // 简化: 剔除 close 相对 intrinsic 偏离更大的
            double call_intrinsic = std::max(call_c.spot - call_c.strike, 0.0);
            double put_intrinsic = std::max(call_c.strike - call_c.spot, 0.0);
            double call_excess = call_c.close - call_intrinsic;
            double put_excess = put_c.close - put_intrinsic;

            int remove_idx = (call_excess > put_excess) ? put_idx : call_idx;
            to_remove.insert(remove_idx);

            stats.removed_L5_parity++;
            stats.removed_contracts.push_back({
                contracts[remove_idx].contract_name,
                "L5",
                fmt::format("parity deviation={:.4f}>{:.2f}", deviation, _cfg.parity_tolerance)
            });
        }
    }

    // 从后往前删除,避免索引错位
    if (!to_remove.empty()) {
        Vector<size_t> sorted_remove(to_remove.begin(), to_remove.end());
        std::sort(sorted_remove.rbegin(), sorted_remove.rend());
        for (size_t idx : sorted_remove) {
            contracts.erase(contracts.begin() + idx);
        }
    }
}
