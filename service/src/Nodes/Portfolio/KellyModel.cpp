#include "Nodes/Portfolio/KellyModel.h"
#include "Util/system.h"

#include <algorithm>
#include <variant>

namespace portfolio {

bool KellyModel::onInit(const nlohmann::json& config) {
    if (config.contains("params")) {
        _min_samples = readInt(config["params"], "kelly_min_samples", _min_samples);
        if (_min_samples < 1) {
            _min_samples = 1;
        }
    }
    return true;
}

void KellyModel::Prepare() {
    // 告警限频状态按运行复位 —— 否则上一轮回测的告警集合会带到这一轮
    _warned_groups.clear();
}

Map<symbol_t, double> KellyModel::compute(PortfolioContext& ctx) {
    if (ctx._groups.empty() || ctx._data == nullptr) {
        return {};
    }

    DataContext& context = *ctx._data;

    // 没有组级统计时：每组均分预算（与改造前"回退 1/N_buys"同语义，
    // 差别只在分母从"买入标的数"变成"组数"—— 配对的两条腿是**一组**）。
    const double fallback = 1.0 / static_cast<double>(ctx._groups.size());

    Vector<double> group_weights;
    group_weights.reserve(ctx._groups.size());

    for (const auto& group : ctx._groups) {
        const GroupStats stats = readGroupStats(context, group._key);

        double f = fallback;
        if (stats._valid && stats._samples >= _min_samples && stats._avg_loss > 0.0) {
            // f* = (b·p − q)/b
            const double b = stats._avg_win / stats._avg_loss;
            const double p = stats._win_rate;
            const double q = 1.0 - p;
            f = (b * p - q) / b;
            if (f < 0.0) {
                // 负的优势 → 不下注。凯利在这里的正确用法是"告诉你别做"，
                // 而不是硬凑一个正数继续下注。
                f = 0.0;
            }
        } else if (stats._valid && stats._samples > 0 && stats._samples < _min_samples) {
            // 每根 bar 都会命中 → 限频，否则实盘会刷屏
            if (_warned_groups.insert(group._id).second) {
                ctx.warn(fmt::format(
                    "Kelly skipped for group '{}': only {} closed trade(s) < min {}; "
                    "fallback to equal deployment", group._key, stats._samples,
                    _min_samples));
            }
        }

        group_weights.push_back(f);
    }

    // 组内按 strength（= 1 : β）拆分 —— 凯利只决定组内**总尺度**。
    return applyCaps(expandGroupWeights(ctx._groups, group_weights));
}

}  // namespace portfolio
