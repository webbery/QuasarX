#include "Nodes/Portfolio/StrengthModel.h"

namespace portfolio {

Vector<double> StrengthModel::computeScales(PortfolioContext& ctx) {
    const size_t groups = groupCount(ctx._legs);
    if (groups == 0) {
        return {};
    }

    // 组间等权；组内拆分由 share 表达（distribute 负责）。
    return Vector<double>(groups, 1.0 / static_cast<double>(groups));
}

}  // namespace portfolio
