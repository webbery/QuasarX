#include "Nodes/Portfolio/VolatilityTargetModel.h"
#include "Util/system.h"

#include <variant>

namespace portfolio {

bool VolatilityTargetModel::onInit(const nlohmann::json& config) {
    if (config.contains("params")) {
        _vol_target = readNumber(config["params"], "volatility_target", _vol_target);
    }
    return true;
}

namespace {

/// 读 context 里某个标量序列的最新值
bool readLast(DataContext& context, const String& key, double& out) {
    if (!context.exist(key)) return false;
    const auto& var = context.get(key);
    const auto* vec = std::get_if<Vector<double>>(&var);
    if (vec == nullptr || vec->empty()) return false;
    out = vec->back();
    return std::isfinite(out);
}

}  // namespace

Map<symbol_t, double> VolatilityTargetModel::compute(PortfolioContext& ctx) {
    if (ctx._groups.empty() || ctx._data == nullptr) {
        return {};
    }

    DataContext& context = *ctx._data;

    // 没有波动率数据时：每组均分预算（与 Kelly 无组级统计同语义）。
    // 注意不能退化成 0 —— 那会让整个组合空仓。
    const double fallback = 1.0 / static_cast<double>(ctx._groups.size());

    Vector<double> group_weights;
    group_weights.reserve(ctx._groups.size());

    for (const auto& group : ctx._groups) {
        // 组级波动率：优先 {key}.volatility（配对组 = 价差的波动率）。
        // 没有就按 strength 权重合成腿级 σ：
        //   σ_group = |Σ_i dir_i · strength_i · σ_i| / Σ_i strength_i
        // 配对的两条腿方向相反 → 多空互换不改变这个值（整体取负）。
        double sigma = 0.0;
        if (!readLast(context, group._key + ".volatility", sigma) || sigma <= 0.0) {
            sigma = 0.0;
            double strength_sum = 0.0;
            for (const auto& symbol : group._legs) {
                double leg_sigma = 0.0;
                if (!readLast(context, get_symbol(symbol) + ".volatility", leg_sigma)) {
                    continue;
                }
                if (leg_sigma <= 0.0) continue;
                const auto st = group._strengths.find(symbol);
                const double strength = (st != group._strengths.end()) ? st->second : 1.0;
                const auto dir = group._dirs.find(symbol);
                const double sign = (dir != group._dirs.end()) ? static_cast<double>(dir->second) : 1.0;
                sigma += sign * strength * leg_sigma;
                strength_sum += strength;
            }
            sigma = (strength_sum > 0.0) ? std::fabs(sigma) / strength_sum : 0.0;
        }

        const double scale = (sigma > 0.0) ? (_vol_target / sigma) : fallback;
        group_weights.push_back(scale);
    }

    // 目标波动率给出的是**组内总尺度**，组内两腿按 strength 拆分。
    return applyCaps(expandGroupWeights(ctx._groups, group_weights));
}

}  // namespace portfolio
