#include "Nodes/Portfolio/MvoModel.h"

#include "Algorithms/MVO/MvoEstimator.h"
#include "Algorithms/MVO/MvoRisk.h"
#include "Algorithms/MVO/MvoOptimizer.h"
#include "Util/system.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace portfolio {

namespace {

/// 收益面板：行=时间，列=资产
struct ReturnsPanel {
    Eigen::MatrixXd  _returns;
    Vector<symbol_t> _symbols;   // 列顺序

    bool valid() const {
        return _returns.rows() > 0 && _returns.cols() > 0;
    }
};

/// 从 DataContext 的 {symbol}.close 序列构造对数收益面板。
///
/// 口径：`.close` 是**后复权**收盘价（QuoteNode 写的是 _adj_close），
/// 所以对数收益跨除权日不需要额外调整。
///
/// 对齐：各标的序列长度可能不一致（缺失 / 停牌 / 晚上市），右对齐到共同
/// 尾部长度 —— 与 Handler/PortfolioHandler.cpp 的 alignToMinLength 同口径。
/// MeanEstimator 内部还会比对"已消费部分是否被改动"，一旦右对齐窗口发生
/// 错位会自动从头重算，不会带着错位的历史继续递推。
ReturnsPanel buildReturnsPanel(DataContext& context, const Set<symbol_t>& pool) {
    ReturnsPanel out;

    // 只存指针 —— 每根 bar 都构造一次面板，按值拷贝整段序列没必要
    Vector<const Vector<double>*> closes;
    closes.reserve(pool.size());
    for (const auto& sym : pool) {
        const String key = get_symbol(sym) + ".close";
        if (!context.exist(key)) continue;
        const auto& var = context.get(key);
        const auto* vec = std::get_if<Vector<double>>(&var);
        if (vec == nullptr || vec->size() < 2) continue;
        closes.push_back(vec);
        out._symbols.push_back(sym);
    }
    if (out._symbols.size() < 2) return out;

    size_t min_len = closes[0]->size();
    for (const auto* c : closes) min_len = std::min(min_len, c->size());
    if (min_len < 2) return out;

    const int T = static_cast<int>(min_len) - 1;
    const int n = static_cast<int>(closes.size());
    out._returns.resize(T, n);
    for (int i = 0; i < n; ++i) {
        const auto& c = *closes[static_cast<size_t>(i)];
        const size_t off = c.size() - min_len;   // 右对齐偏移
        for (int t = 0; t < T; ++t) {
            const double p0 = c[off + static_cast<size_t>(t)];
            const double p1 = c[off + static_cast<size_t>(t) + 1];
            out._returns(t, i) =
                (std::isfinite(p0) && std::isfinite(p1) && p0 > 0.0 && p1 > 0.0)
                    ? std::log(p1 / p0)
                    : std::numeric_limits<double>::quiet_NaN();
        }
    }
    return out;
}

mvo::MeanMethod parseMeanMethod(const String& s) {
    return (s == "sample") ? mvo::MeanMethod::Sample : mvo::MeanMethod::Kalman;
}

mvo::CovMethod parseCovMethod(const String& s) {
    if (s == "sample") return mvo::CovMethod::Sample;
    if (s == "ledoit_wolf") return mvo::CovMethod::LedoitWolf;
    return mvo::CovMethod::EwmaShrunk;
}

}  // namespace

MvoModel::MvoModel()
    : _estimator(new mvo::MeanEstimator())
    , _risk(new mvo::RiskModel()) {
    _method = SizingMethod::MVO;
}

MvoModel::~MvoModel() {
    delete _estimator;
    delete _risk;
}

bool MvoModel::onInit(const nlohmann::json& config) {
    if (!config.contains("params")) {
        return true;
    }
    const auto& params = config["params"];

    _mean_method = readString(params, "mvo_mean_method", _mean_method);
    _cov_method = readString(params, "mvo_cov_method", _cov_method);

    _risk_aversion = readNumber(params, "mvo_risk_aversion", _risk_aversion);
    _turnover_penalty = readNumber(params, "mvo_turnover_penalty", _turnover_penalty);
    _turnover_horizon = readNumber(params, "mvo_turnover_horizon", _turnover_horizon);
    _cost_rate = readNumber(params, "mvo_cost_rate", _cost_rate);
    _max_weight = readNumber(params, "mvo_max_weight", _max_weight);
    _vol_target = readNumber(params, "mvo_vol_target", _vol_target);
    _vol_target_enabled = readBool(params, "mvo_vol_target_enabled", _vol_target_enabled);
    _min_samples = readInt(params, "mvo_min_samples", _min_samples);
    _min_names = readInt(params, "mvo_min_names", _min_names);

    // 分支算法参数一次性下发到估计器 —— 它们的递推状态跟配置强相关，
    // 之后每根 bar 不再 setConfig（否则每根 bar 都会重置滤波器）。
    mvo::MeanEstimatorConfig mean_cfg;
    mean_cfg._method = parseMeanMethod(_mean_method);
    _estimator->setConfig(mean_cfg);

    mvo::RiskModelConfig risk_cfg;
    risk_cfg._method = parseCovMethod(_cov_method);
    _risk->setConfig(risk_cfg);

    return true;
}

void MvoModel::Prepare() {
    // 卡尔曼滤波器 / EWMA 协方差 / 上期权重都是跨 bar 递推的，每次运行开始
    // 必须重置 —— 否则上一次回测的尾部状态会污染这一次。
    if (_estimator) _estimator->reset();
    if (_risk) _risk->reset();
    _prev_weights.clear();
}

Map<symbol_t, double> MvoModel::compute(PortfolioContext& ctx) {
    if (ctx._data == nullptr) {
        return {};
    }
    DataContext& context = *ctx._data;

    // ── 候选集：有 BUY 信号且当前无持仓 ──
    // 已持仓的 BUY 标的在 generatePlan 里会走 "already has position, skipping
    // buy"，拿不到资金；把它们纳入优化只会把预算稀释到不会被执行的名字上。
    Vector<symbol_t> candidates;
    candidates.reserve(ctx._buy_symbols.size());
    for (const auto& symbol : ctx._buy_symbols) {
        if (ctx._get_position_qty && ctx._get_position_qty(symbol) != 0) {
            continue;
        }
        candidates.push_back(symbol);
    }

    if (static_cast<int>(candidates.size()) < _min_names) {
        ctx.warn(fmt::format(
            "MVO skipped: {} fresh BUY candidate(s) < min {}; fallback to equal weight",
            candidates.size(), _min_names));
        return {};
    }

    // ── 收益面板 ──
    // 用整个交易池而不只是候选集：因子剥离需要全市场等权收益做市场代理。
    if (ctx._symbol_pool == nullptr) {
        ctx.warn("MVO skipped: symbol pool unavailable; fallback to equal weight");
        return {};
    }
    ReturnsPanel panel = buildReturnsPanel(context, *ctx._symbol_pool);
    if (!panel.valid()) {
        ctx.warn("MVO skipped: no valid '.close' series (fast-backtest mode?); "
                 "fallback to equal weight");
        return {};
    }
    if (static_cast<int>(panel._returns.rows()) < _min_samples) {
        ctx.warn(fmt::format("MVO skipped: {} return sample(s) < min {}; "
                             "fallback to equal weight",
                             panel._returns.rows(), _min_samples));
        return {};
    }

    // ── 均值 + 协方差（均为年化口径）──
    const mvo::MeanEstimate mean = _estimator->update(panel._returns);
    const Eigen::MatrixXd cov = _risk->update(panel._returns);

    if (!mean._valid ||
        mean._mu_total.size() != static_cast<size_t>(panel._symbols.size())) {
        ctx.warn("MVO skipped: mean estimator produced no valid estimate; "
                 "fallback to equal weight");
        return {};
    }

    // ── 候选在面板中的列 ──
    Vector<symbol_t> picked;
    Vector<int> cols;
    picked.reserve(candidates.size());
    cols.reserve(candidates.size());
    for (const auto& symbol : candidates) {
        const auto it = std::find(panel._symbols.begin(), panel._symbols.end(), symbol);
        if (it == panel._symbols.end()) continue;   // 该标的没有 .close 数据
        picked.push_back(symbol);
        cols.push_back(static_cast<int>(it - panel._symbols.begin()));
    }
    if (static_cast<int>(picked.size()) < _min_names) {
        ctx.warn(fmt::format("MVO skipped: only {} candidate(s) have '.close' data "
                             "(< min {}); fallback to equal weight",
                             picked.size(), _min_names));
        return {};
    }

    const size_t m = picked.size();
    Eigen::VectorXd mu(static_cast<Eigen::Index>(m));
    Eigen::VectorXd w_prev(static_cast<Eigen::Index>(m));
    Eigen::MatrixXd cov_sub(static_cast<Eigen::Index>(m), static_cast<Eigen::Index>(m));
    for (size_t i = 0; i < m; ++i) {
        const int ci = cols[i];
        mu(static_cast<Eigen::Index>(i)) = mean._mu_total[static_cast<size_t>(ci)];
        const auto prev = _prev_weights.find(picked[i]);
        w_prev(static_cast<Eigen::Index>(i)) =
            (prev != _prev_weights.end()) ? prev->second : 0.0;
        for (size_t j = 0; j < m; ++j) {
            cov_sub(static_cast<Eigen::Index>(i), static_cast<Eigen::Index>(j)) =
                cov(ci, cols[j]);
        }
    }

    // ── 求解 ──
    mvo::MvoParams params;
    params._method = mvo::OptimizerMethod::Ospq;
    params._risk_aversion = _risk_aversion;
    params._turnover_penalty = _turnover_penalty;
    // 成本是一次性的（占净值比例），目标函数是年化的 → 必须乘"一年能做多少
    // 次这样的换手" = YEAR_DAY / H 才能同量纲。不折算的话惩罚项会被稀释
    // 1~2 个数量级、完全不起作用（lab 实测 κ=1 时开关惩罚项换手 87.20 vs 87.10）。
    params._cost_rate = _cost_rate * static_cast<double>(YEAR_DAY) /
                        std::max(_turnover_horizon, 1.0);
    params._max_weight = _max_weight;
    params._max_total_exposure = 1.0;
    params._vol_target = _vol_target;
    params._vol_target_enabled = _vol_target_enabled;

    mvo::MvoOptimizer optimizer(params);
    const mvo::MvoResult result = optimizer.solve(mu, cov_sub, w_prev);
    if (!result._ok || result._weights.size() != m) {
        ctx.warn(fmt::format("MVO solve failed ({}); fallback to equal weight",
                             result._note));
        return {};
    }

    // ── 写回权重 ──
    Map<symbol_t, double> weights;
    Map<symbol_t, double> next_prev;
    for (size_t i = 0; i < m; ++i) {
        const double w = result._weights[i];
        weights[picked[i]] = w;
        next_prev[picked[i]] = w;
    }
    _prev_weights = std::move(next_prev);

    // 不走 applyCaps：QP 已带 0 ≤ w ≤ w_max 与 Σw = e，再比例缩放会破坏
    // 等式约束（预算分配就会少一截）。
    return weights;
}

}  // namespace portfolio
