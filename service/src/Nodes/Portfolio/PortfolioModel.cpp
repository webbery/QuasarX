#include "Nodes/Portfolio/PortfolioModel.h"

#include "Nodes/Portfolio/EqualModel.h"
#include "Nodes/Portfolio/KellyModel.h"
#include "Nodes/Portfolio/VolatilityTargetModel.h"
#include "Nodes/Portfolio/StrengthModel.h"
#include "Nodes/Portfolio/MvoModel.h"
#include "Util/system.h"

#include <algorithm>
#include <cmath>

namespace portfolio {

String to_string(SizingMethod method) {
    switch (method) {
        case SizingMethod::Equal:            return "equal";
        case SizingMethod::Kelly:            return "kelly";
        case SizingMethod::VolatilityTarget: return "volatility_target";
        case SizingMethod::MVO:              return "mvo";
        case SizingMethod::BlackLitter:      return "black_litter";
        case SizingMethod::Strength:         return "strength";
    }
    return "equal";
}

SizingMethod parseSizingMethod(const String& text) {
    if (text == "kelly") return SizingMethod::Kelly;
    if (text == "volatility_target") return SizingMethod::VolatilityTarget;
    if (text == "mvo") return SizingMethod::MVO;
    if (text == "black_litter") return SizingMethod::BlackLitter;
    if (text == "strength") return SizingMethod::Strength;
    return SizingMethod::Equal;
}

// ── 组的腿区间 ───────────────────────────────────────────────

Vector<size_t> groupOffsets(const Vector<PortfolioLeg>& legs) {
    Vector<size_t> offsets;
    if (legs.empty()) {
        return offsets;
    }
    offsets.push_back(0);
    for (size_t i = 1; i < legs.size(); ++i) {
        // 同组 = 非 0 且与上一条腿相同；_group == 0 的腿各自成组
        const bool same_group =
            legs[i]._group != 0 && legs[i]._group == legs[i - 1]._group;
        if (!same_group) {
            offsets.push_back(i);
        }
    }
    offsets.push_back(legs.size());
    return offsets;
}

size_t groupCount(const Vector<PortfolioLeg>& legs) {
    const auto offsets = groupOffsets(legs);
    return offsets.empty() ? 0 : offsets.size() - 1;
}

// ── PortfolioModel ────────────────────────────────────────────

bool PortfolioModel::Init(const nlohmann::json& config, const Set<symbol_t>& universe) {
    onUniverse(universe);
    if (!config.contains("params")) {
        return onInit(config);
    }
    const auto& params = config["params"];

    _max_single_pct = readNumber(params, "max_single_pct", _max_single_pct);
    // 旧键 max_total_pct 的语义本来就是"总敞口 Σ|w|"，保留兼容；
    // 新键 max_gross_pct 更直白，同时给出时以新键为准。
    _max_gross_pct = readNumber(params, "max_total_pct", _max_gross_pct);
    _max_gross_pct = readNumber(params, "max_gross_pct", _max_gross_pct);
    _max_net_pct = readNumber(params, "max_net_pct", _max_net_pct);

    return onInit(config);
}

Map<symbol_t, double> PortfolioModel::compute(PortfolioContext& ctx) {
    const size_t groups = groupCount(ctx._legs);

    Vector<double> scales = computeScales(ctx);
    if (scales.size() != groups) {
        if (!scales.empty()) {
            ctx.warn(fmt::format("{}: computeScales returned {} scale(s) for {} group(s); "
                                 "fallback to equal deployment",
                                 name(), scales.size(), groups));
        }
        scales.assign(groups, groups > 0 ? 1.0 / static_cast<double>(groups) : 0.0);
    }

    Map<symbol_t, double> weights = distribute(ctx, scales);
    return capsOnOutput() ? applyCaps(std::move(weights), ctx) : weights;
}

Map<symbol_t, double> PortfolioModel::distribute(const PortfolioContext& ctx,
                                                 const Vector<double>& scales) {
    Map<symbol_t, double> weights;
    const auto offsets = groupOffsets(ctx._legs);
    const size_t groups = offsets.empty() ? 0 : offsets.size() - 1;
    const size_t limit = std::min(groups, scales.size());

    for (size_t g = 0; g < limit; ++g) {
        const double scale = scales[g];
        for (size_t i = offsets[g]; i < offsets[g + 1]; ++i) {
            const auto& leg = ctx._legs[i];
            weights[leg._symbol] = scale * static_cast<double>(leg._share);
        }
    }
    return weights;
}

Map<symbol_t, double> PortfolioModel::applyCaps(Map<symbol_t, double> weights,
                                                const PortfolioContext& ctx) const {
    if (weights.empty()) {
        return weights;
    }

    // 方向查表：净敞口要按 dir 求和，腿表是唯一权威来源
    Map<symbol_t, int> dirs;
    for (const auto& leg : ctx._legs) {
        dirs[leg._symbol] = leg._dir;
    }

    double gross = 0.0;
    for (auto& [symbol, w] : weights) {
        w = std::min(float(w), _max_single_pct);
        gross += w;
    }

    // 总敞口上限（多空合并的 Σ|w|）
    if (gross > _max_gross_pct && gross > 0.0) {
        const double scale = _max_gross_pct / gross;
        for (auto& [symbol, w] : weights) {
            w *= scale;
        }
        gross = _max_gross_pct;
    }

    // 净敞口上限 |Σ dir·w|：把超过的一侧等比压回去，组内比例不变
    if (_max_net_pct > 0.0) {
        double long_gross = 0.0;
        double short_gross = 0.0;
        for (const auto& [symbol, w] : weights) {
            const auto it = dirs.find(symbol);
            const int dir = (it != dirs.end()) ? it->second : 1;
            (dir >= 0 ? long_gross : short_gross) += w;
        }

        const double net = long_gross - short_gross;
        if (std::fabs(net) > _max_net_pct) {
            const bool long_excess = net > 0.0;
            const double big = long_excess ? long_gross : short_gross;
            const double small = long_excess ? short_gross : long_gross;
            // 大边压到 max_net + small，净敞口正好落在上限上
            const double target = std::max(0.0, _max_net_pct + small);
            if (big > 0.0) {
                const double scale = std::min(1.0, target / big);
                for (auto& [symbol, w] : weights) {
                    const auto it = dirs.find(symbol);
                    const int dir = (it != dirs.end()) ? it->second : 1;
                    if ((dir >= 0) == long_excess) {
                        w *= scale;
                    }
                }
            }
        }
    }

    return weights;
}

std::unique_ptr<PortfolioModel> createPortfolioModel(SizingMethod method) {
    switch (method) {
        case SizingMethod::Equal:            return std::make_unique<EqualModel>();
        case SizingMethod::Kelly:            return std::make_unique<KellyModel>();
        case SizingMethod::VolatilityTarget: return std::make_unique<VolatilityTargetModel>();
        case SizingMethod::Strength:         return std::make_unique<StrengthModel>();
        case SizingMethod::MVO:              return std::make_unique<MvoModel>();
        case SizingMethod::BlackLitter:      break;   // 未实现 → 回退等权
    }
    return std::make_unique<EqualModel>();
}

}  // namespace portfolio
