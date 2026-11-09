#include "Nodes/Portfolio/VolatilityTargetModel.h"
#include "Util/system.h"

#include <cmath>
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

Vector<double> VolatilityTargetModel::computeScales(PortfolioContext& ctx) {
    const auto offsets = groupOffsets(ctx._legs);
    const size_t groups = offsets.empty() ? 0 : offsets.size() - 1;
    if (groups == 0) {
        return {};
    }

    // 没有波动率数据时：每组均分预算（与 Kelly 无组级统计同语义）。
    // 注意不能退化成 0 —— 那会让整个组合空仓。
    const double fallback = 1.0 / static_cast<double>(groups);

    Vector<double> scales(groups, 0.0);
    for (size_t g = 0; g < groups; ++g) {
        const auto& anchor = ctx._legs[offsets[g]];

        // 组级波动率：优先 {key}.volatility（配对组 = 价差的波动率）。
        // 没有就按 share 合成腿级 σ：
        //   σ_group = |Σ_i dir_i · share_i · σ_i| / Σ_i share_i
        // 配对的两条腿方向相反 → 多空互换不改变这个值（整体取负）。
        double sigma = 0.0;
        if (!readLast(ctx._data, groupKeyOf(ctx, anchor) + ".volatility", sigma) ||
            sigma <= 0.0) {
            sigma = 0.0;
            double share_sum = 0.0;
            for (size_t i = offsets[g]; i < offsets[g + 1]; ++i) {
                const auto& leg = ctx._legs[i];
                double leg_sigma = 0.0;
                if (!readLast(ctx._data, get_symbol(leg._symbol) + ".volatility", leg_sigma)) {
                    continue;
                }
                if (leg_sigma <= 0.0) {
                    continue;
                }
                sigma += static_cast<double>(leg._dir) * leg._share * leg_sigma;
                share_sum += leg._share;
            }
            sigma = (share_sum > 0.0) ? std::fabs(sigma) / share_sum : 0.0;
        }

        scales[g] = (sigma > 0.0) ? (_vol_target / sigma) : fallback;
    }

    // 目标波动率给出的是**组内总尺度**，组内按 share 拆分由基类完成。
    return scales;
}

}  // namespace portfolio
