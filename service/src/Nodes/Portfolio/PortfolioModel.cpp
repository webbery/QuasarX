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

Map<symbol_t, double> expandGroupWeights(
    const Vector<PortfolioGroup>& groups,
    const Vector<double>& group_weights) {
    Map<symbol_t, double> weights;
    const size_t n = std::min(groups.size(), group_weights.size());
    for (size_t i = 0; i < n; ++i) {
        const auto& group = groups[i];
        const double group_weight = group_weights[i];
        for (const auto& symbol : group._legs) {
            weights[symbol] = group_weight * group.shareOf(symbol);
        }
    }
    return weights;
}

GroupStats readGroupStats(DataContext& context, const String& key) {
    GroupStats stats;
    const auto readOne = [&](const char* field, double& out) -> bool {
        const String k = key + "." + field;
        if (!context.exist(k)) {
            return false;
        }
        const auto& var = context.get(k);
        const auto* vec = std::get_if<Vector<double>>(&var);
        if (vec == nullptr || vec->empty()) {
            return false;
        }
        out = vec->back();
        return std::isfinite(out);
    };

    double win_rate = 0.0;
    if (!readOne("win_rate", win_rate)) {
        return stats;
    }
    stats._win_rate = std::clamp(win_rate, 0.0, 1.0);
    // avg_win / avg_loss 缺失时按对称赔率 1:1 处理 —— 硬编码 2%/2% 是错的：
    // 配对的单笔盈亏幅度差一个数量级（实测 +5.9% / −7.6%），赔率直接决定
    // 凯利比例，猜错等于把仓位算错。
    double avg_win = 0.0;
    double avg_loss = 0.0;
    const bool has_win = readOne("avg_win", avg_win);
    const bool has_loss = readOne("avg_loss", avg_loss);
    stats._avg_win = has_win ? std::max(avg_win, 0.0) : 0.02;
    stats._avg_loss = has_loss ? std::max(avg_loss, 0.0) : 0.02;
    double samples = 0.0;
    if (readOne("samples", samples)) {
        stats._samples = static_cast<int>(std::max(samples, 0.0));
    } else {
        // 没有样本数时按 1 笔处理 —— 调用方（KellyModel）会用自己的最小样本
        // 门槛再挡一次，不会拿 1 笔的胜率当结论。
        stats._samples = 1;
    }
    stats._valid = true;
    return stats;
}

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
