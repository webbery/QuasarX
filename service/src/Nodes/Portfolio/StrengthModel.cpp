#include "Nodes/Portfolio/StrengthModel.h"
#include "Util/system.h"

#include <algorithm>
#include <variant>

namespace portfolio {

Map<symbol_t, double> StrengthModel::compute(PortfolioContext& ctx) {
    Map<symbol_t, double> weights;
    if (ctx._buy_symbols.empty() || ctx._data == nullptr) {
        return weights;
    }

    DataContext& context = *ctx._data;
    const double fallback = 1.0 / static_cast<double>(ctx._buy_symbols.size());

    double total = 0.0;
    for (const auto& symbol : ctx._buy_symbols) {
        const String key = get_symbol(symbol) + ".strength";

        double strength = 0.0;
        if (context.exist(key)) {
            const auto& var = context.get(key);
            if (const auto* vec = std::get_if<Vector<double>>(&var);
                vec != nullptr && !vec->empty()) {
                strength = vec->back();
            }
        }

        // 负值或零值给一个最小权重，避免完全排除
        strength = std::max(strength, 1e-6);
        weights[symbol] = strength;
        total += strength;
    }

    if (total > 0.0) {
        for (auto& [symbol, w] : weights) {
            w /= total;
        }
    } else {
        for (const auto& symbol : ctx._buy_symbols) {
            weights[symbol] = fallback;
        }
    }

    return applyCaps(std::move(weights));
}

}  // namespace portfolio
