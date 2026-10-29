#include "Nodes/SpreadNode.h"
#include "Util/log.h"
#include <cmath>
#include <algorithm>

namespace {
/// 从 "{symbol}.{field}" 里取出 symbol（前缀含一个点，如 sz.000333）
String symbolOf(const String& key) {
    auto dot = key.rfind('.');
    if (dot == String::npos) return "";
    return key.substr(0, dot);
}
}  // namespace

SpreadNode::SpreadNode()
: _method(SpreadMethod::SIMPLE_DIFF)
, _window(60)
, _fixedBeta(1.0)
, _priceCount(0)
, _priceNext(0)
{
}

SpreadNode::~SpreadNode() {
}

bool SpreadNode::Init(const nlohmann::json& config) {
    // 价差计算方式
    if (config["params"].contains("method")) {
        String method = config["params"]["method"]["value"];
        if (method == "simple_diff") {
            _method = SpreadMethod::SIMPLE_DIFF;
        } else if (method == "log_diff") {
            _method = SpreadMethod::LOG_DIFF;
        } else if (method == "rolling_regression") {
            _method = SpreadMethod::ROLLING_REGRESSION;
        } else {
            WARN("[SpreadNode] unknown method '{}', fallback to simple_diff", method);
        }
    }

    // 回归窗口
    if (config["params"].contains("window")) {
        int32_t w = config["params"]["window"]["value"];
        _window = std::max(2, w);
    }

    // 固定 β（simple_diff / log_diff 使用）
    if (config["params"].contains("beta")) {
        _fixedBeta = config["params"]["beta"]["value"];
    }

    // 显式指定分子腿；为空则按上游发现顺序
    if (config["params"].contains("symbolA")) {
        _symbolA = (String)config["params"]["symbolA"]["value"];
    }

    // 取哪个价格字段（边的 sourceHandle 优先）
    if (config["params"].contains("priceField")) {
        _priceField = (String)config["params"]["priceField"]["value"];
    }

    _priceA.assign(_window, 0.0);
    _priceB.assign(_window, 0.0);
    _priceCount = 0;
    _priceNext = 0;

    // 两条腿在这里就解析出来：下游节点（DebugNode 等）在 Init 之后读
    // out_elements() 来建输出列，腿没解析出来就没有任何列。
    if (!resolveLegs()) {
        return false;
    }

    return true;
}

void SpreadNode::Prepare(const String& strategy, DataContext& context) {
    // 每次 run（回测 / 日终回放 / 实时）开始时复位滚动窗口。
    // 窗口内容必须是"本次 run 内、t-1 及以前"的价格，否则滞后估计的
    // 时间口径会被上一轮残留污染。
    _priceCount = 0;
    _priceNext = 0;
    std::fill(_priceA.begin(), _priceA.end(), 0.0);
    std::fill(_priceB.begin(), _priceB.end(), 0.0);
}

/// 从上游解析两条腿：symbol + 价格序列 key
///
/// 支持两种接线方式：
///   1. 两个行情输入节点，每个一只标的；
///   2. 一个行情输入节点，code 里放两只标的。
/// 字段名优先取边上的 sourceHandle（形如 "1-close"），否则用 priceField。
bool SpreadNode::resolveLegs() {
    struct Cand {
        String symbol;
        String key;
    };
    Vector<Cand> cands;

    for (auto& [targetHandle, upstream] : _ins) {
        // 找本节点在该上游上的边，从 sourceHandle 解析字段名
        String field = _priceField;
        for (auto& [srcHandle, downstream] : upstream->outs()) {
            if (downstream != this) continue;
            auto dash = srcHandle.find('-');
            if (dash != String::npos) {
                String f = srcHandle.substr(dash + 1);
                if (!f.empty()) field = f;
            }
            break;
        }

        String suffix = "." + field;
        for (auto& [key, type] : upstream->out_elements()) {
            if (key.size() <= suffix.size()) continue;
            if (key.compare(key.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            String sym = symbolOf(key);
            if (sym.empty()) continue;
            // 同一 symbol 只取第一个匹配的 key
            bool dup = false;
            for (auto& c : cands) {
                if (c.symbol == sym) { dup = true; break; }
            }
            if (!dup) cands.push_back({sym, key});
        }
    }

    if (cands.size() != 2) {
        WARN("[SpreadNode] expected exactly 2 legs, got {} ({})", cands.size(),
             cands.empty() ? "none" : cands[0].symbol);
        return false;
    }

    // 分子腿：显式指定优先，否则取发现顺序的第一个
    int aIdx = 0;
    if (!_symbolA.empty()) {
        bool found = false;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (cands[i].symbol == _symbolA) { aIdx = static_cast<int>(i); found = true; break; }
        }
        if (!found) {
            WARN("[SpreadNode] symbolA='{}' not among upstream symbols, use discovery order",
                 _symbolA);
        }
    }
    int bIdx = 1 - aIdx;

    _legA._symbol = cands[aIdx].symbol;
    _legA._priceKey = cands[aIdx].key;
    _legA._leg = 1.0;
    _legB._symbol = cands[bIdx].symbol;
    _legB._priceKey = cands[bIdx].key;
    _legB._leg = -1.0;

    INFO("[SpreadNode] legs: A={} (leg=+1) B={} (leg=-1) method={} window={}",
         _legA._symbol, _legB._symbol, static_cast<int>(_method), _window);
    return true;
}

/// 读取当根价格；任一腿非有限（NaN / 缺失 / <=0）则返回 false
bool SpreadNode::currentPrices(DataContext& context, double& pa, double& pb) const {
    auto readLast = [&](const String& key, double& out) -> bool {
        if (!context.exist(key)) return false;
        auto& var = context.get(key);
        auto* vec = std::get_if<Vector<double>>(&var);
        if (!vec || vec->empty()) return false;
        out = vec->back();
        return std::isfinite(out) && out > 0.0;
    };

    if (!readLast(_legA._priceKey, pa)) return false;
    if (!readLast(_legB._priceKey, pb)) return false;
    return true;
}

/// 滞后窗口 OLS：窗口内容为 t-1 及以前
bool SpreadNode::estimateBeta(double& alpha, double& beta) const {
    if (_priceCount < 2) return false;

    // 窗口未满时只统计已填入的前 count 个；填满后整圈都有效（顺序对 OLS 无影响）
    int32_t n = (_priceCount < _window) ? _priceCount : _window;

    double sumA = 0.0, sumB = 0.0;
    for (int32_t i = 0; i < n; ++i) {
        sumA += _priceA[i];
        sumB += _priceB[i];
    }
    double meanA = sumA / n;
    double meanB = sumB / n;

    double covAB = 0.0, varB = 0.0;
    for (int32_t i = 0; i < n; ++i) {
        double da = _priceA[i] - meanA;
        double db = _priceB[i] - meanB;
        covAB += da * db;
        varB += db * db;
    }
    if (varB < 1e-12) return false;

    beta = covAB / varB;
    alpha = meanA - beta * meanB;
    return true;
}

/// 按 method 计算当前价差
double SpreadNode::computeSpread(double pa, double pb, double alpha, double beta) const {
    switch (_method) {
    case SpreadMethod::LOG_DIFF:
        return std::log(pa) - beta * std::log(pb);
    case SpreadMethod::ROLLING_REGRESSION:
        return pa - (alpha + beta * pb);
    case SpreadMethod::SIMPLE_DIFF:
    default:
        return pa - beta * pb;
    }
}

/// 把当根价格对推进环形缓冲（窗口内容变为 t 及以前，供下一根 bar 估计）
void SpreadNode::pushPrice(double pa, double pb) {
    if (_priceCount < _window) {
        _priceA[_priceCount] = pa;
        _priceB[_priceCount] = pb;
        ++_priceCount;
    } else {
        _priceA[_priceNext] = pa;
        _priceB[_priceNext] = pb;
        _priceNext = (_priceNext + 1) % _window;
    }
}

/// 往 context 追加一个标量（每根 bar 一条，NaN 也追加）
void SpreadNode::publish(DataContext& context, const String& symbol, const String& field,
                         double value) {
    String key = symbol + "." + field;
    if (context.exist(key)) {
        context.add(key, value);
    } else {
        Vector<double> ts;
        ts.push_back(value);
        context.set(key, ts);
    }
}

NodeProcessResult SpreadNode::Process(const String& strategy, DataContext& context) {
    if (_legA._symbol.empty() || _legB._symbol.empty()) {
        if (!resolveLegs()) {
            return NodeProcessResult::Error;
        }
    }

    double pa = 0.0, pb = 0.0;
    bool valid = currentPrices(context, pa, pb);

    double spread = std::nan("");
    double beta = _fixedBeta;
    double alpha = 0.0;

    if (valid) {
        if (_method == SpreadMethod::ROLLING_REGRESSION) {
            double a = 0.0, b = 0.0;
            if (estimateBeta(a, b)) {
                alpha = a;
                beta = b;
            } else {
                // 窗口还没攒够（或 B 价无波动）→ 本根不算价差
                valid = false;
            }
        }
        if (valid) {
            spread = computeSpread(pa, pb, alpha, beta);
            if (!std::isfinite(spread)) {
                valid = false;
                spread = std::nan("");
            }
        }
    }

    if (valid) {
        // 有效价格对推进窗口（下一根 bar 才能用它估 β）
        pushPrice(pa, pb);
    }

    // 两腿各发一份同值序列
    publish(context, _legA._symbol, "pair_spread", spread);
    publish(context, _legB._symbol, "pair_spread", spread);
    publish(context, _legA._symbol, "pair_beta", beta);
    publish(context, _legB._symbol, "pair_beta", beta);
    publish(context, _legA._symbol, "pair_leg", _legA._leg);
    publish(context, _legB._symbol, "pair_leg", _legB._leg);

    return NodeProcessResult::Success;
}

Map<String, ArgType> SpreadNode::out_elements() {
    Map<String, ArgType> elems;
    if (_legA._symbol.empty() || _legB._symbol.empty()) {
        return elems;
    }
    for (const String& sym : {_legA._symbol, _legB._symbol}) {
        elems[sym + ".pair_spread"] = ArgType::Double_TimeSeries;
        elems[sym + ".pair_beta"] = ArgType::Double_TimeSeries;
        elems[sym + ".pair_leg"] = ArgType::Double_TimeSeries;
    }
    return elems;
}

const nlohmann::json SpreadNode::getParams() {
    return nlohmann::json{
        {"method", "string"},
        {"window", "number"},
        {"beta", "number"},
        {"symbolA", "string"},
        {"priceField", "string"}
    };
}
