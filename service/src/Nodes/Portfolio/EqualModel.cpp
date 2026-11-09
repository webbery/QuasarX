#include "Nodes/Portfolio/EqualModel.h"

namespace portfolio {

Vector<double> EqualModel::computeScales(PortfolioContext& /*ctx*/) {
    // 空 = 不介入 → 基类按 1/G 等权兜底
    return {};
}

}  // namespace portfolio
