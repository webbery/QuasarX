#include "Nodes/Portfolio/StrengthModel.h"
#include "Util/system.h"

#include <algorithm>
#include <variant>

namespace portfolio {

Map<symbol_t, double> StrengthModel::compute(PortfolioContext& ctx) {
    if (ctx._groups.empty() || ctx._data == nullptr) {
        return {};
    }

    // 组内按 strength 归一化 → 1 : β（配对）；组间均分预算。
    //
    // 组内归一化是关键：配对的两条腿方向相反，只算 BUY 腿的话空头腿拿不到
    // strength 权重 → 它退回 PortfolioNode 的兜底公式 targetCapital/count
    // → 腿名义比恒为 1:2 而不是 1:β（PairNode 发布的 {B}.strength = β
    // 形同虚设）。按组归一化后 weight_A = 1/(1+β)、weight_B = β/(1+β)，
    // 与 backtest_pair.py 的 not_a = E/(1+β)、not_b = E·β/(1+β) 同口径。
    //
    // 按组（而不是按全部建仓腿）归一化，多组配对时各组之间不会互相抢权重。
    const double group_weight = 1.0 / static_cast<double>(ctx._groups.size());
    Vector<double> group_weights(ctx._groups.size(), group_weight);

    return applyCaps(expandGroupWeights(ctx._groups, group_weights));
}

}  // namespace portfolio
