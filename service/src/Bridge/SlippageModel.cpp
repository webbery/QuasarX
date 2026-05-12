#include "Bridge/SlippageModel.h"
#include "spdlog/spdlog.h"
#include <cmath>

using namespace spdlog;

// ============================================================
// FixedRatioSlippage
// ============================================================
bool FixedRatioSlippage::Init(const nlohmann::json& config) {
    if (config.contains("ratio")) {
        _ratio = config["ratio"].get<double>();
    }
    return _ratio >= 0;
}

// ============================================================
// VolumeImpactSlippage
// ============================================================
bool VolumeImpactSlippage::Init(const nlohmann::json& config) {
    if (config.contains("base"))    _base    = config["base"].get<double>();
    if (config.contains("impact_k")) _impactK = config["impact_k"].get<double>();
    if (config.contains("alpha"))   _alpha   = config["alpha"].get<double>();
    return _base >= 0 && _impactK >= 0 && _alpha > 0 && _alpha <= 1;
}

double VolumeImpactSlippage::calculate(const SlippageContext& ctx) const {
    // 防护：quote 成交量为 0 时回退到基础滑点
    if (ctx.quote._volume == 0) {
        return ctx.basePrice * _base;
    }

    double volumeRatio = static_cast<double>(ctx.order._volume) / static_cast<double>(ctx.quote._volume);
    double impactSlip = _impactK * std::pow(volumeRatio, _alpha);
    return ctx.basePrice * (_base + impactSlip);
}

// ============================================================
// SlippageFactory
// ============================================================
std::unique_ptr<ISlippageModel> SlippageFactory::create(const nlohmann::json& config) {
    int type = 0;  // 默认固定比例
    if (config.contains("type")) {
        type = config["type"].get<int>();
    }

    switch (type) {
    case 0: {  // fixed_ratio
        auto model = std::make_unique<FixedRatioSlippage>();
        model->Init(config);
        return model;
    }
    case 1: {  // volume_impact
        auto model = std::make_unique<VolumeImpactSlippage>();
        model->Init(config);
        return model;
    }
    default:
        WARN("Unknown slippage model type: {}, fallback to fixed_ratio", type);
        auto model = std::make_unique<FixedRatioSlippage>();
        model->Init(config);
        return model;
    }
}
