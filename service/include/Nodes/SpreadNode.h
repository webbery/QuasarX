#pragma once
#include "StrategyNode.h"

enum class SpreadMethod {
    SIMPLE_DIFF,         // P_A - beta * P_B
    LOG_DIFF,            // ln(P_A) - beta * ln(P_B)
    ROLLING_REGRESSION,  // P_A - (alpha + beta * P_B)，alpha/beta 由滞后窗口 OLS 估计
};

/**
 * 价差节点 —— 计算两条腿之间的价差与对冲系数。
 *
 * 无状态、无持仓概念、不产生任何交易动作，只输出数值。
 *
 * 时间口径：
 *   β 的估计只用 t-1 及以前的价格，窗口内容为 [t-W, t-1]，当根价格只参与外推。
 *   若用含当日的窗口做回归，当日残差会被自己"拉回"，价差与后续的偏离度
 *   会被系统性压低，入场次数偏少。
 *
 * 输出（两条腿各发布一份同值序列，便于下游按 {symbol}.xxx 寻址）：
 *   {sym}.pair_spread  当前价差
 *   {sym}.pair_beta    当前对冲系数（simple/log 模式为固定 β）
 *   {sym}.pair_leg     腿方向：分子腿(A) = +1，分母腿(B) = -1
 *
 * 数据健壮性：
 *   任一腿当根价格非有限（停牌被填 NaN、缺失、<=0）时，不更新任何滚动窗口，
 *   价差输出 NaN，并返回 Success —— 返回 Skip 会中断整个 epoch，
 *   一条腿停牌就整轮不出决策。
 *   价差序列每根 bar 都追加（含 NaN），下游遇 NaN 可自行跳过而不挪动统计窗口。
 */
class SpreadNode : public QNode {
public:
    RegistClassName(SpreadNode);
    static const nlohmann::json getParams();

    SpreadNode();
    virtual ~SpreadNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual void Prepare(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;

private:
    struct Leg {
        String _symbol;      // "sz.000333"
        String _priceKey;    // context 中的价格序列 key
        double _leg = 1.0;   // +1 = 分子腿(A)，-1 = 分母腿(B)
    };

    /// 从上游解析出两条腿（symbol + 价格序列 key）
    bool resolveLegs();
    /// 读取当根价格；任一腿非有限则返回 false
    bool currentPrices(DataContext& context, double& pa, double& pb) const;
    /// 滞后窗口 OLS：窗口内容为 t-1 及以前
    bool estimateBeta(double& alpha, double& beta) const;
    /// 按 method 计算当前价差
    double computeSpread(double pa, double pb, double alpha, double beta) const;

    void pushPrice(double pa, double pb);
    void publish(DataContext& context, const String& symbol, const String& field,
                 double value);

private:
    SpreadMethod _method = SpreadMethod::SIMPLE_DIFF;
    int32_t _window = 60;     // β 回归窗口（bar）
    double _fixedBeta = 1.0;   // simple/log 模式下的固定 β
    String _symbolA;          // 显式指定分子腿；为空则按发现顺序
    String _priceField = "close";  // 从上游取哪个价格字段

    Leg _legA;
    Leg _legB;

    // 价格环形缓冲（内容为 t-1 及以前，只存有效价格对）
    Vector<double> _priceA;
    Vector<double> _priceB;
    int32_t _priceCount = 0;
    int32_t _priceNext = 0;
};
