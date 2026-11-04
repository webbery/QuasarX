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

Map<symbol_t, double> VolatilityTargetModel::compute(PortfolioContext& ctx) {
    Map<symbol_t, double> weights;
    if (ctx._buy_symbols.empty() || ctx._data == nullptr) {
        return weights;
    }

    DataContext& context = *ctx._data;
    const double fallback = 1.0 / static_cast<double>(ctx._buy_symbols.size());

    for (const auto& symbol : ctx._buy_symbols) {
        const String key = get_symbol(symbol) + ".volatility";

        if (context.exist(key)) {
            const auto& var = context.get(key);
            if (const auto* vec = std::get_if<Vector<double>>(&var);
                vec != nullptr && !vec->empty()) {
                const double sigma = vec->back();
                if (sigma > 0.0) {
                    weights[symbol] = _vol_target / sigma;
                }
            }
        } else {
            weights[symbol] = fallback;
        }
    }

    return applyCaps(std::move(weights));
}

}  // namespace portfolio
