#pragma once
#include "std_header.h"
#include "Bridge/exchange.h"
#include "json.hpp"

// ============================================================
// 滑点模型 — 回测/仿真中用于估算实际成交价
// ============================================================

struct SlippageContext {
    const Order&       order;        // 订单信息
    const QuoteInfo&   quote;        // 当前 K 线/行情
    double             basePrice;    // 基准价（订单报价）
};

class ISlippageModel {
public:
    virtual ~ISlippageModel() = default;
    virtual String name() const = 0;

    /**
     * 计算滑点绝对值（正值）
     * BUY:  成交价 = basePrice + slip
     * SELL: 成交价 = basePrice - slip
     */
    virtual double calculate(const SlippageContext& ctx) const = 0;
    virtual bool Init(const nlohmann::json& config) = 0;
};

// ============================================================
// 固定比例滑点
// ============================================================
class FixedRatioSlippage : public ISlippageModel {
public:
    String name() const override { return "fixed_ratio"; }
    double calculate(const SlippageContext& ctx) const override {
        return ctx.basePrice * _ratio;
    }
    bool Init(const nlohmann::json& config) override;
private:
    double _ratio = 0.001;  // 默认 0.1%
};

// ============================================================
// 成交量冲击滑点（简化版 Almgren-Chriss）
// slipRatio = base + k * (orderVolume / quoteVolume)^alpha
// ============================================================
class VolumeImpactSlippage : public ISlippageModel {
public:
    String name() const override { return "volume_impact"; }
    double calculate(const SlippageContext& ctx) const override;
    bool Init(const nlohmann::json& config) override;
private:
    double _base = 0.001;      // 基础滑点 0.1%
    double _impactK = 0.01;    // 冲击系数
    double _alpha = 0.5;       // 冲击幂次
};

// ============================================================
// 工厂
// ============================================================
class SlippageFactory {
public:
    static std::unique_ptr<ISlippageModel> create(const nlohmann::json& config);
};
