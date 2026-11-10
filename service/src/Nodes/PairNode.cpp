#include "Nodes/PairNode.h"
#include "DataContext.h"
#include "Nodes/Portfolio/PortfolioModel.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "std_header.h"
#include <cmath>

namespace {

// 止损锁（_blocked）挡掉入场的 bar 数。线程局部：回测循环固定跑在一个计算线程上。
// 止损后策略要求 |z| 回落到 exitZ 以内才允许重开，这段"什么都不做"的状态过去
// 既不写 signal 也不写日志 —— 排查"为什么这里没开仓"时完全无从下手。
thread_local int t_blockedBars = 0;

// 被 β/体制过滤挡掉的入场次数（每次 run 复位），只用于首条 INFO 提示
thread_local int t_filteredEntries = 0;

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
    if (config["params"].contains("weightMode")) {
        _weightMode = to_lower((String)config["params"]["weightMode"]["value"]);
    }
    if (config["params"].contains("betaMin")) {
        _betaMin = config["params"]["betaMin"]["value"];
    }
    if (config["params"].contains("betaMax")) {
        _betaMax = config["params"]["betaMax"]["value"];
    }
    if (config["params"].contains("betaCvMax")) {
        _betaCvMax = config["params"]["betaCvMax"]["value"];
    }
    if (config["params"].contains("betaCvWindow")) {
        _betaCvWindow = std::max<int32_t>(10, config["params"]["betaCvWindow"]["value"]);
    }
    if (config["params"].contains("regimeTrendMax")) {
        _regimeTrendMax = config["params"]["regimeTrendMax"]["value"];
    }
    if (config["params"].contains("regimeTrendWindow")) {
        _regimeTrendWindow = std::max<int32_t>(1, config["params"]["regimeTrendWindow"]["value"]);
    }
    if (config["params"].contains("regimeSigmaWindow")) {
        _regimeSigmaWindow = std::max<int32_t>(5, config["params"]["regimeSigmaWindow"]["value"]);
    }
    INFO("[PairNode:{}] weightMode={} beta∈[{},{}] betaCvMax={} "
         "regimeTrendMax={} (win={}, sigma={})",
         _id, _weightMode, _betaMin, _betaMax, _betaCvMax,
         _regimeTrendMax, _regimeTrendWindow, _regimeSigmaWindow);

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
    t_filteredEntries = 0;
    // 往返统计也要复位 —— 否则上一次回测的尾部统计会污染这一次
    _stats = PairTradeStats{};
    _in_trade = false;
}

bool PairNode::readLast(DataContext& context, const String& key, double& out) {
    if (!context.exist(key)) return false;
    auto& var = context.get(key);
    auto* vec = std::get_if<Vector<double>>(&var);
    if (!vec || vec->empty()) return false;
    out = vec->back();
    return std::isfinite(out);
}

bool PairNode::readPrevPrice(DataContext& context, const String& symbol, double& out) {
    // 成交价取的是**上一根 bar** 的收盘价，成交发生在本根 bar
    // （PortfolioNode 的 getQuotePrice → GetPrimitivePrice）。
    // 统计盈亏必须同一口径 → 取序列的倒数第二个值。
    // 撮合/估值一律用**原始价**（项目约定：分析用后复权价，撮合用原始价）。
    for (const char* prop : {"org_close_w", "close"}) {
        const String key = symbol + "." + prop;
        if (!context.exist(key)) continue;
        const auto& var = context.get(key);
        const auto* vec = std::get_if<Vector<double>>(&var);
        if (vec == nullptr || vec->size() < 2) continue;
        const double v = (*vec)[vec->size() - 2];
        if (std::isfinite(v) && v > 0.0) {
            out = v;
            return true;
        }
    }
    return false;
}

bool PairNode::readPrevAdjPrice(DataContext& context, const String& symbol, double& out) {
    // 分析价格空间：后复权收盘价（价差/β/配腿权重的基准）
    for (const char* prop : {"close", "close_w"}) {
        const String key = symbol + "." + prop;
        if (!context.exist(key)) continue;
        const auto& var = context.get(key);
        const auto* vec = std::get_if<Vector<double>>(&var);
        if (vec == nullptr || vec->size() < 2) continue;
        const double v = (*vec)[vec->size() - 2];
        if (std::isfinite(v) && v > 0.0) {
            out = v;
            return true;
        }
    }
    return false;
}

bool PairNode::readLastN(DataContext& context, const String& key, int32_t n, Vector<double>& out) {
    if (n <= 0 || !context.exist(key)) return false;
    const auto& var = context.get(key);
    const auto* vec = std::get_if<Vector<double>>(&var);
    if (vec == nullptr || vec->empty()) return false;
    const int32_t count = std::min<int32_t>(n, static_cast<int32_t>(vec->size()));
    out.clear();
    out.reserve(count);
    const size_t start = vec->size() - static_cast<size_t>(count);
    for (size_t i = start; i < vec->size(); ++i) {
        out.push_back((*vec)[i]);
    }
    return true;
}

bool PairNode::readLastPrice(DataContext& context, const String& symbol, bool preferAdj,
                             double& out) {
    static const char* kOrgProps[] = {"org_close", "close", "org_close_w", "close_w"};
    static const char* kAdjProps[] = {"close", "org_close", "close_w", "org_close_w"};
    const char** props = preferAdj ? kAdjProps : kOrgProps;
    for (int i = 0; i < 4; ++i) {
        const String key = symbol + "." + props[i];
        if (!context.exist(key)) continue;
        const auto& var = context.get(key);
        const auto* vec = std::get_if<Vector<double>>(&var);
        if (vec == nullptr || vec->empty()) continue;
        const double v = vec->back();
        if (std::isfinite(v) && v > 0.0) {
            out = v;
            return true;
        }
    }
    return false;
}

bool PairNode::entryFiltersPass(DataContext& context, double betaRaw) const {
    // ── β 有效性 / 区间 ──
    if (!std::isfinite(betaRaw) || betaRaw <= 0.0) {
        return false;   // 负 β / 未就绪：价差没有经济含义，禁止开仓
    }
    if (_betaMin > 0.0 && betaRaw < _betaMin) return false;
    if (_betaMax > 0.0 && betaRaw > _betaMax) return false;

    // ── β 稳定性：滚动 CV = std/mean ──
    if (_betaCvMax > 0.0) {
        Vector<double> hist;
        if (!readLastN(context, get_symbol(_legA) + ".pair_beta", _betaCvWindow, hist)) {
            return false;
        }
        double sum = 0.0, sumSq = 0.0;
        int32_t cnt = 0;
        for (double v : hist) {
            if (!std::isfinite(v)) continue;
            sum += v;
            sumSq += v * v;
            ++cnt;
        }
        if (cnt < 10) return false;
        const double mean = sum / cnt;
        const double var = sumSq / cnt - mean * mean;
        if (mean <= 1e-9 || var < 0.0) return false;
        const double cv = std::sqrt(var) / mean;
        if (cv > _betaCvMax) return false;
    }

    // ── 体制过滤：价差自身的趋势强度 ──
    // |S_t - S_{t-k}| / σ_S > threshold ⇒ 价差处在单边趋势里，
    // 均值回复的入场假设不成立（2025–2026 的亏损都发生在这种区间）。
    if (_regimeTrendMax > 0.0) {
        const int32_t need = std::max(_regimeSigmaWindow, _regimeTrendWindow + 1);
        Vector<double> spread;
        if (!readLastN(context, get_symbol(_legA) + ".pair_spread", need, spread)) {
            return false;
        }
        if (static_cast<int32_t>(spread.size()) <= _regimeTrendWindow) return false;
        const double sNow = spread.back();
        const double sPast = spread[spread.size() - 1 - _regimeTrendWindow];
        if (!std::isfinite(sNow) || !std::isfinite(sPast)) return false;

        double sum = 0.0, sumSq = 0.0;
        int32_t cnt = 0;
        const size_t start = spread.size() > static_cast<size_t>(_regimeSigmaWindow)
            ? spread.size() - static_cast<size_t>(_regimeSigmaWindow) : 0;
        for (size_t i = start; i < spread.size(); ++i) {
            if (!std::isfinite(spread[i])) continue;
            sum += spread[i];
            sumSq += spread[i] * spread[i];
            ++cnt;
        }
        if (cnt < 10) return false;
        const double mean = sum / cnt;
        const double var = sumSq / cnt - mean * mean;
        if (var <= 0.0) return false;
        const double sigma = std::sqrt(var);
        if (sigma <= 1e-12) return false;
        if (std::fabs(sNow - sPast) / sigma > _regimeTrendMax) return false;
    }

    return true;
}

void PairNode::recordTradeOpen(DataContext& context, double beta, int dirA) {
    double pxA = 0.0;
    double pxB = 0.0;
    if (!readPrevPrice(context, get_symbol(_legA), pxA) ||
        !readPrevPrice(context, get_symbol(_legB), pxB)) {
        _in_trade = false;
        return;
    }
    // 名义权重：与 PortfolioNode 的 strength 拆分同口径
    //   notional_beta：w_A = 1/(1+β),          w_B = β/(1+β)
    //   price_spread ：w_A = P_A_hfq/(P_A_hfq + β·P_B_hfq), w_B = 1 - w_A
    //     （名义按后复权价分摊 → 实际股数比 f_A : β·f_B）
    double wA = 1.0 / (1.0 + beta);
    double wB = beta / (1.0 + beta);
    if (_weightMode == "price_spread") {
        double hA = 0.0, hB = 0.0;
        if (readPrevAdjPrice(context, get_symbol(_legA), hA) &&
            readPrevAdjPrice(context, get_symbol(_legB), hB) &&
            hA > 0.0 && hB > 0.0) {
            const double denom = hA + beta * hB;
            if (denom > 1e-12) {
                wA = hA / denom;
                wB = beta * hB / denom;
            }
        }
    }
    _in_trade = true;
    _entry_px_a = pxA;
    _entry_px_b = pxB;
    _entry_w_a = wA;
    _entry_w_b = wB;
    _dir_a = dirA;   // 分母腿方向恒为 -dirA，不单独存
    _trade_beta = beta;
}

void PairNode::recordTradeClose(DataContext& context) {
    if (!_in_trade) {
        return;
    }
    double pxA = 0.0;
    double pxB = 0.0;
    const bool ok = readPrevPrice(context, get_symbol(_legA), pxA) &&
                    readPrevPrice(context, get_symbol(_legB), pxB);
    const int dirA = _dir_a;
    const int dirB = -_dir_a;   // 配对两腿方向必然相反
    const double entryA = _entry_px_a;
    const double entryB = _entry_px_b;
    _in_trade = false;
    if (!ok) {
        return;
    }

    // 组合收益（equity-unit 口径）：π = w_A·dir_A·r_A + w_B·dir_B·r_B
    //   r 为**原始价**收益（撮合/估值口径），w 为入场时两腿的名义权重。
    //   notional_beta: w=(1,β)/(1+β) → 与旧公式 (dir_A·r_A + β·dir_B·r_B)/(1+β) 完全一致
    //   price_spread : w=(P_A_hfq, β·P_B_hfq)/合计 → 与实际建仓仓位一致
    const double rA = pxA / entryA - 1.0;
    const double rB = pxB / entryB - 1.0;
    const double pi = _entry_w_a * dirA * rA + _entry_w_b * dirB * rB;
    if (!std::isfinite(pi)) {
        return;
    }
    ++_stats._trades;
    if (pi > 0.0) {
        ++_stats._wins;
        _stats._sum_win += pi;
    } else {
        _stats._sum_loss += -pi;
    }
}

void PairNode::publishTradeStats(DataContext& context, uint32_t groupId) {
    if (_stats._trades <= 0) {
        return;
    }
    const String key = portfolio::groupKey(groupId, _legA);
    const double p = static_cast<double>(_stats._wins) / static_cast<double>(_stats._trades);
    const int losses = _stats._trades - _stats._wins;
    const double avg_win = (_stats._wins > 0) ? _stats._sum_win / _stats._wins : 0.0;
    const double avg_loss = (losses > 0) ? _stats._sum_loss / losses : 0.0;
    publishScalar(context, key, "win_rate", p);
    publishScalar(context, key, "avg_win", avg_win);
    publishScalar(context, key, "avg_loss", avg_loss);
    publishScalar(context, key, "samples", static_cast<double>(_stats._trades));
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

    // ── 腿间权重 ──
    // notional_beta：A=1, B=β（名义比 1:β，收益空间对冲）
    // price_spread ：A=P_A, B=β·P_B（名义比 P_A:βP_B，价格水平价差对冲）。
    //   价差/β 定义在后复权价上 → 权重也必须用后复权价算：名义按 P_A_hfq : βP_B_hfq
    //   分摊，实际股数 = 名义 / 原始价 → 股数比 = f_A : β·f_B，
    //   原始价 P&L 因此正比于 Δ(A_hfq - β·B_hfq)（撮合/估值仍在原始价体系）。
    double strengthA = 1.0;
    double strengthB = beta;
    if (_weightMode == "price_spread") {
        double pxA = 0.0, pxB = 0.0;
        const bool preferAdj = true;   // 分析价格空间：后复权
        if (readLastPrice(context, get_symbol(_legA), preferAdj, pxA) &&
            readLastPrice(context, get_symbol(_legB), preferAdj, pxB) &&
            pxA > 0.0 && pxB > 0.0) {
            strengthA = pxA;
            strengthB = beta * pxB;
        } else if (!_warnedNoPrice) {
            WARN("[PairNode:{}] price_spread 配腿读不到 {} 的后复权收盘价，"
                 "退回名义 1:β（检查上游 close 字段是否发布）",
                 _id, get_symbol(_legA));
            _warnedNoPrice = true;
        }
    }
    publishScalar(context, get_symbol(_legA), "strength", strengthA);
    publishScalar(context, get_symbol(_legB), "strength", strengthB);

    // ── 定仓组标识 ──
    // 两腿同值。pair_leg 只有 ±1，一个策略里存在多个 PairNode 时，
    // 下游无法判断哪条 B 属于哪条 A —— PortfolioNode 的组原子下单
    // （开仓要么整组都下、要么都不下）按这个 key 分组。
    // 取 symbol 的 31 位哈希：double 能精确表示，且不依赖 symbol_t 的内部布局。
    //
    // group_id 是通用键（任何生产者都可以发布，配对只是其中一种）；
    // pair_id 保留一个版本，兼容旧策略图 / 旧调试列。
    const double pairId = static_cast<double>(
        std::hash<symbol_t>{}(_legA) & 0x7FFFFFFFull);
    publishScalar(context, get_symbol(_legA), "group_id", pairId);
    publishScalar(context, get_symbol(_legB), "group_id", pairId);
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
            // β 有效/稳定 + 体制过滤：不满足就不建仓（只拦入场，不拦出场）
            if (!entryFiltersPass(context, betaRaw)) {
                ++t_filteredEntries;
                if (t_filteredEntries == 1) {
                    INFO("[PairNode:{}] 入场被过滤（β={:.3f} 或价差趋势不达标），"
                         "本次运行后续同类过滤不再逐条打印", _id, betaRaw);
                }
            } else {
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

    // ── 往返统计（凯利的输入来源）──
    // 必须在门控**之后**记：门控可能把一条腿降级成 HOLD，那种情况下仓位
    // 根本没建起来/没平掉，记进去就是一笔假交易。
    // 判定：两腿都在 decisions 里、且方向一多一空 = 完整的一腿开仓或平仓。
    if (decisions.size() == 2 && decisions.count(_legA) && decisions.count(_legB)) {
        const bool aBuy = decisions[_legA] == TradeAction::BUY;
        const bool bBuy = decisions[_legB] == TradeAction::BUY;
        if (aBuy != bBuy) {
            if (inPosition) {
                recordTradeClose(context);
            } else {
                recordTradeOpen(context, beta, aBuy ? 1 : -1);
            }
        }
    }
    publishTradeStats(context, static_cast<uint32_t>(pairId));

    writeSignals(context, decisions);

    return NodeProcessResult::Success;
}

Map<String, ArgType> PairNode::out_elements() {
    Map<String, ArgType> elems = SignalNodeBase::out_elements();
    for (auto& symbol : _pools) {
        elems[get_symbol(symbol) + ".strength"] = ArgType::Double_TimeSeries;
        // 定仓组标识（两腿同值），供 PortfolioNode 做组原子下单
        elems[get_symbol(symbol) + ".group_id"] = ArgType::Double_TimeSeries;
        // 兼容旧策略图 / 旧调试列的别名
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
        {"allowShort", "boolean"},
        {"weightMode", "string"},        // notional_beta | price_spread
        {"betaMin", "number"},
        {"betaMax", "number"},
        {"betaCvMax", "number"},
        {"betaCvWindow", "number"},
        {"regimeTrendMax", "number"},
        {"regimeTrendWindow", "number"},
        {"regimeSigmaWindow", "number"}
    };
}
