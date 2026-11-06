#include "Nodes/PairNode.h"
#include "DataContext.h"
#include "Util/log.h"
#include "Util/system.h"
#include "std_header.h"
#include <cmath>

namespace {

// 止损锁（_blocked）挡掉入场的 bar 数。线程局部：回测循环固定跑在一个计算线程上。
// 止损后策略要求 |z| 回落到 exitZ 以内才允许重开，这段"什么都不做"的状态过去
// 既不写 signal 也不写日志 —— 排查"为什么这里没开仓"时完全无从下手。
thread_local int t_blockedBars = 0;

}  // namespace

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
    t_blockedBars = 0;
}

bool PairNode::readLast(DataContext& context, const String& key, double& out) {
    if (!context.exist(key)) return false;
    auto& var = context.get(key);
    auto* vec = std::get_if<Vector<double>>(&var);
    if (!vec || vec->empty()) return false;
    out = vec->back();
    return std::isfinite(out);
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
    publishScalar(context, get_symbol(_legA), "strength", 1.0);
    publishScalar(context, get_symbol(_legB), "strength", beta);

    // ── 配对分组标识 ──
    // 两腿同值。pair_leg 只有 ±1，一个策略里存在多个 PairNode 时，
    // 下游无法判断哪条 B 属于哪条 A —— PortfolioNode 的两腿原子下单
    // （开仓要么两腿都下、要么都不下）按这个 key 分组。
    // 取 symbol 的 31 位哈希：double 能精确表示，且不依赖 symbol_t 的内部布局。
    const double pairId = static_cast<double>(
        std::hash<symbol_t>{}(_legA) & 0x7FFFFFFFull);
    publishScalar(context, get_symbol(_legA), "pair_id", pairId);
    publishScalar(context, get_symbol(_legB), "pair_id", pairId);

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
    } else if (inPosition && (qA == 0) != (qB == 0)) {
        // ── 裸腿自愈 ──
        // 配对只剩一条腿在账上：上一根 bar 的另一腿被拒（融券无券源 / 资金不足 /
        // 涨跌停）或被 T+1 缩减。此时持仓是未对冲的方向暴露，不能按"持有中"
        // 等 |z| 回归 —— 立刻平掉残余腿回到空仓。成交在 bar 末才确定，下一根
        // bar 的 PairNode 是最早能拿到真实持仓的节点，所以这里就是最早的自愈点。
        if (qA > 0) actions.push_back({_legA, TradeAction::SELL});
        if (qA < 0) actions.push_back({_legA, TradeAction::BUY});
        if (qB > 0) actions.push_back({_legB, TradeAction::SELL});
        if (qB < 0) actions.push_back({_legB, TradeAction::BUY});
        WARN("[PairNode:{}] 配对出现裸腿：A({}) qty={}, B({}) qty={}；"
             "已发出平掉残余腿的指令，并在 |z| 回落到 exitZ={} 之前禁止重新开仓",
             _id, get_symbol(_legA), qA, get_symbol(_legB), qB, _exitZ);
        _barsHeld = 0;
        _blocked = true;
        t_blockedBars = 0;
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
        // 止损后需等价差回到 exitZ 之内才允许再次开仓。
        // 缺这一步时 _blocked 只会在"持仓分支"里被复位，而止损后已经是空仓，
        // 永远进不到那个分支 → 一次止损就把策略永久锁死
        if (_blocked && absZ <= _exitZ) {
            _blocked = false;
            t_blockedBars = 0;
        }
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
        } else if (_blocked && absZ >= _entryZ) {
            // 已越过入场阈值却因为止损锁而空仓等待 —— 每个锁定区间只提示一次，
            // 之后只累加计数，避免在阈值附近来回震荡时刷屏
            ++t_blockedBars;
            if (t_blockedBars == 1) {
                WARN("[PairNode:{}] 入场被止损锁抑制：|z|={:.2f} >= entryZ={:.2f}，"
                     "但上次止损后 |z| 尚未回落到 exitZ={:.2f} 以内；"
                     "在 z 回归前不会重复开仓",
                     _id, absZ, _entryZ, _exitZ);
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
        // 配对分组标识（两腿同值），供 PortfolioNode 做两腿原子下单
        elems[get_symbol(symbol) + ".pair_id"] = ArgType::Double_TimeSeries;
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
