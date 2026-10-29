#include "Nodes/PairNode.h"
#include "DataContext.h"
#include "Util/log.h"
#include "Util/system.h"
#include "std_header.h"
#include <cmath>

PairNode::PairNode(Server* server) : SignalNodeBase(server) {
}

PairNode::~PairNode() {
}

bool PairNode::Init(const nlohmann::json& config) {
    // 基类：allowShort + 上游标的发现 + 可做空名单
    if (!SignalNodeBase::Init(config)) {
        return false;
    }

    if (config["params"].contains("entryZ")) {
        _entryZ = config["params"]["entryZ"]["value"];
    }
    if (config["params"].contains("exitZ")) {
        _exitZ = config["params"]["exitZ"]["value"];
    }
    if (config["params"].contains("stopZ")) {
        _stopZ = config["params"]["stopZ"]["value"];
    }
    if (config["params"].contains("maxHoldBars")) {
        _maxHoldBars = config["params"]["maxHoldBars"]["value"];
    }
    if (config["params"].contains("zField")) {
        _zField = (String)config["params"]["zField"]["value"];
    }

    if (_exitZ >= _entryZ) {
        WARN("[PairNode:{}] exitZ({}) >= entryZ({}), 入场后立即触发出场条件",
             _id, _exitZ, _entryZ);
    }
    if (_stopZ <= _entryZ) {
        WARN("[PairNode:{}] stopZ({}) <= entryZ({}), 入场即触发止损",
             _id, _stopZ, _entryZ);
    }

    return true;
}

void PairNode::Prepare(const String& strategy, DataContext& context) {
    // 每次 run 开始时复位状态机：持有 bar 数与止损锁
    _barsHeld = 0;
    _blocked = false;
}

bool PairNode::readLast(DataContext& context, const String& key, double& out) {
    if (!context.exist(key)) return false;
    auto& var = context.get(key);
    auto* vec = std::get_if<Vector<double>>(&var);
    if (!vec || vec->empty()) return false;
    out = vec->back();
    return std::isfinite(out);
}

void PairNode::publish(DataContext& context, const String& symbol, const String& field,
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

NodeProcessResult PairNode::Process(const String& strategy, DataContext& context) {
    // ── 解析两条腿 ──
    if (_legA == null_symbol() || _legB == null_symbol()) {
        for (auto sym : _pools) {
            double leg = 0.0;
            if (!readLast(context, get_symbol(sym) + ".pair_leg", leg)) continue;
            if (leg > 0) {
                _legA = sym;
            } else if (leg < 0) {
                _legB = sym;
            }
        }
        if (_legA == null_symbol() || _legB == null_symbol()) {
            if (!_warnedNoLeg) {
                WARN("[PairNode:{}] 未能从上游解析出配对的两条腿（需要 pair_leg=+1 与 -1）",
                     _id);
                _warnedNoLeg = true;
            }
            return NodeProcessResult::Success;
        }
        INFO("[PairNode:{}] legs: A={} B={} entryZ={} exitZ={} stopZ={} maxHoldBars={}",
             _id, get_symbol(_legA), get_symbol(_legB), _entryZ, _exitZ, _stopZ,
             _maxHoldBars);
    }

    // ── 名义金额权重：分子腿 1，分母腿 |β| ──
    double beta = 1.0;
    double betaRaw = 0.0;
    if (readLast(context, get_symbol(_legA) + ".pair_beta", betaRaw) && betaRaw > 0.0) {
        beta = betaRaw;
    }
    publish(context, get_symbol(_legA), "strength", 1.0);
    publish(context, get_symbol(_legB), "strength", beta);

    // ── 当前偏离度 ──
    double z = std::nan("");
    readLast(context, get_symbol(_legA) + "." + _zField, z);

    // ── 账户持仓 ──
    Map<symbol_t, int64_t> held = loadPositions(strategy);
    auto qtyOf = [&](symbol_t sym) -> int64_t {
        auto it = held.find(sym);
        return (it == held.end()) ? 0 : it->second;
    };
    int64_t qA = qtyOf(_legA);
    int64_t qB = qtyOf(_legB);
    bool inPosition = (qA != 0 || qB != 0);

    List<Pair<symbol_t, TradeAction>> actions;

    if (!std::isfinite(z)) {
        // 偏离度不可用（warmup 未满 / 一条腿停牌）→ 不动作
        _barsHeld = 0;
    } else if (inPosition) {
        double absZ = std::fabs(z);
        bool exit = false;
        if (absZ >= _stopZ) {
            exit = true;
            _blocked = true;   // 止损后锁住再次入场，直到 |z| <= exitZ
        } else if (absZ <= _exitZ) {
            exit = true;
            _blocked = false;
        } else if (_maxHoldBars > 0 && _barsHeld >= _maxHoldBars) {
            exit = true;
        }

        if (exit) {
            // 平两腿：多头腿 SELL，空头腿 BUY
            if (qA > 0) actions.push_back({_legA, TradeAction::SELL});
            if (qA < 0) actions.push_back({_legA, TradeAction::BUY});
            if (qB > 0) actions.push_back({_legB, TradeAction::SELL});
            if (qB < 0) actions.push_back({_legB, TradeAction::BUY});
            _barsHeld = 0;
        } else {
            // 保持：持仓方向不变，不发任何动作
            ++_barsHeld;
        }
    } else {
        double absZ = std::fabs(z);
        if (absZ >= _entryZ && !_blocked) {
            // z>0：分子腿偏贵 → 空分子腿、多分母腿；z<0 反之
            symbol_t shortLeg = (z > 0) ? _legA : _legB;
            if (!canOpenShort(shortLeg)) {
                if (!_warnedNoShort) {
                    WARN("[PairNode:{}] 空头腿 {} 不允许做空（allowShort={}，"
                         "或不在 margin_stock 名单），本策略不会建配对仓位",
                         _id, get_symbol(shortLeg), _allowShort);
                    _warnedNoShort = true;
                }
            } else {
                actions.push_back({_legA, (z > 0) ? TradeAction::SELL : TradeAction::BUY});
                actions.push_back({_legB, (z > 0) ? TradeAction::BUY : TradeAction::SELL});
                _barsHeld = 0;
            }
        }
    }

    // ── 门控 + 写 signal ──
    applyShortableFilter(actions, held);
    applyNoAddOnPosition(actions, held);

    Map<symbol_t, TradeAction> decisions;
    for (auto& item : actions) {
        if (item.second != TradeAction::HOLD) {
            decisions[item.first] = item.second;
        }
    }
    writeSignals(context, decisions);

    return NodeProcessResult::Success;
}

Map<String, ArgType> PairNode::out_elements() {
    Map<String, ArgType> elems = SignalNodeBase::out_elements();
    for (auto& symbol : _pools) {
        elems[get_symbol(symbol) + ".strength"] = ArgType::Double_TimeSeries;
    }
    return elems;
}

const nlohmann::json PairNode::getParams() {
    return nlohmann::json{
        {"entryZ", "number"},
        {"exitZ", "number"},
        {"stopZ", "number"},
        {"maxHoldBars", "number"},
        {"zField", "string"},
        {"allowShort", "boolean"}
    };
}
