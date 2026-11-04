#include "Nodes/Portfolio/KellyModel.h"
#include "Util/system.h"

#include <algorithm>
#include <variant>

namespace portfolio {

Map<symbol_t, double> KellyModel::compute(PortfolioContext& ctx) {
    Map<symbol_t, double> weights;
    if (ctx._buy_symbols.empty() || ctx._data == nullptr) {
        return weights;
    }

    DataContext& context = *ctx._data;
    const double fallback = 1.0 / static_cast<double>(ctx._buy_symbols.size());

    for (const auto& symbol : ctx._buy_symbols) {
        const String key = get_symbol(symbol) + ".win_rate";
        bool computed = false;

        if (context.exist(key)) {
            const auto& var = context.get(key);
            if (const auto* vec = std::get_if<Vector<double>>(&var);
                vec != nullptr && !vec->empty()) {
                const double win_rate = vec->back();
                const double avg_win = 0.02;   // TODO: 从上下文读取
                const double avg_loss = 0.02;  // TODO: 从上下文读取
                if (avg_loss > 0.0) {
                    const double b = avg_win / avg_loss;
                    const double q = 1.0 - win_rate;
                    const double kelly = (b * win_rate - q) / b;
                    weights[symbol] = std::max(0.0, kelly);
                    computed = true;
                }
            }
        }

        if (!computed) {
            weights[symbol] = fallback;
        }
    }

    return applyCaps(std::move(weights));
}

}  // namespace portfolio
