#include "Nodes/Portfolio/PortfolioModel.h"

#include "Nodes/Portfolio/EqualModel.h"
#include "Nodes/Portfolio/KellyModel.h"
#include "Nodes/Portfolio/VolatilityTargetModel.h"
#include "Nodes/Portfolio/StrengthModel.h"
#include "Nodes/Portfolio/MvoModel.h"

#include <algorithm>

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

// ── 配置读取（统一英文键）──────────────────────────────────

namespace {

const nlohmann::json* findValue(const nlohmann::json& params, const char* key) {
    if (!params.contains(key)) return nullptr;
    const auto& node = params[key];
    return (node.is_object() && node.contains("value")) ? &node["value"] : nullptr;
}

}  // namespace

double readNumber(const nlohmann::json& params, const char* key, double dflt) {
    const auto* v = findValue(params, key);
    return (v && v->is_number()) ? v->get<double>() : dflt;
}

int readInt(const nlohmann::json& params, const char* key, int dflt) {
    const auto* v = findValue(params, key);
    return (v && v->is_number()) ? v->get<int>() : dflt;
}

bool readBool(const nlohmann::json& params, const char* key, bool dflt) {
    const auto* v = findValue(params, key);
    return (v && v->is_boolean()) ? v->get<bool>() : dflt;
}

String readString(const nlohmann::json& params, const char* key, const String& dflt) {
    const auto* v = findValue(params, key);
    return (v && v->is_string()) ? static_cast<String>(*v) : dflt;
}

// ── PortfolioModel ────────────────────────────────────────────

bool PortfolioModel::Init(const nlohmann::json& config) {
    if (!config.contains("params")) {
        return onInit(config);
    }
    const auto& params = config["params"];

    _max_single_pct = readNumber(params, "max_single_pct", _max_single_pct);
    _max_total_pct = readNumber(params, "max_total_pct", _max_total_pct);

    return onInit(config);
}

Map<symbol_t, double> PortfolioModel::applyCaps(Map<symbol_t, double> weights) const {
    if (weights.empty()) {
        return weights;
    }

    double total = 0.0;
    for (auto& [symbol, w] : weights) {
        w = std::min(w, _max_single_pct);
        total += w;
    }

    if (total > _max_total_pct && total > 0.0) {
        const double scale = _max_total_pct / total;
        for (auto& [symbol, w] : weights) {
            w *= scale;
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
