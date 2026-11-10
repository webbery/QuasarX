#pragma once
#include "Nodes/SignalNodeBase.h"
#include "Util/system.h"

class Server;

/**
 * 配对节点 —— 把一条腿的偏离度翻译成两腿各自的交易动作。
 *
 * 输入（由上游节点发布在 context 里）：
 *   {sym}.pair_z      偏离度序列（通常由 FunctionNode(ZScore) 产生）
 *   {sym}.pair_leg    腿方向：分子腿(A) = +1，分母腿(B) = -1
 *   {sym}.pair_beta   对冲系数，用于按 β 分配名义金额
 *
 * 输出：
 *   {sym}.signal      +1 = 买入，-1 = 卖出，0 = 保持（与 SignalNode 同格式）
 *   {sym}.strength    名义金额权重（分子腿 = 1，分母腿 = |β|）
 *   {sym}.group_id    定仓组标识（两腿同值），下游据此做组原子下单；
 *                     {sym}.pair_id 保留为兼容别名
 *
 * 为什么是持仓感知：
 *   "平多"和"平空"在无状态表达式里无法表达 —— 同一个 SELL，有多仓时是平多，
 *   空仓时会被当成开空。读入账户真实持仓后，每个动作在 PortfolioNode 现有
 *   语义下都是无歧义的：
 *     多仓 → 平仓发 SELL；空仓 → 平空发 BUY；空仓开多发 BUY；空仓开空发 SELL；
 *     持仓方向不变 → 发 0（保持）。
 *   持仓同时充当状态机的真值来源，日终回放重跑历史时不会与账户脱节。
 *
 * 状态机：
 *   空仓 + |z| >= entryZ        → 按 z 的符号建仓（z>0：空分子腿、多分母腿）
 *   持仓 + |z| <= exitZ          → 两腿同时平（均值回归）
 *   持仓 + |z| >= stopZ          → 两腿同时平（止损），并锁住再次入场直到 |z| <= exitZ
 *   持仓 + 持有超过 maxHoldBars  → 两腿同时平（时间止损）
 *   其余                          → 保持
 */
class PairNode : public SignalNodeBase {
public:
    RegistClassName(PairNode);
    static const nlohmann::json getParams();

    PairNode(Server* server);
    virtual ~PairNode();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual void Prepare(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;

protected:
    String nodeTypeName() const override { return className(); }

private:
    /// 读 context 里某 key 的最新值；不存在或非有限返回 false
    static bool readLast(DataContext& context, const String& key, double& out);

    /// 读某个价格序列的**倒数第二个**值（= 上一根 bar 的收盘价）。
    ///
    /// 口径必须与成交价一致：委托价取的是上一根 bar 的收盘价
    /// （PortfolioNode 的 getQuotePrice → GetPrimitivePrice(symbol, curIndex-1)），
    /// 成交发生在本根 bar → 入场价/出场价都用各自那根 bar 的 [-2]。
    /// 撮合/估值一律用**原始价**（项目约定：分析用后复权价，撮合用原始价）。
    bool readPrevPrice(DataContext& context, const String& symbol, double& out);

    /// 读上一根 bar 的**后复权**收盘价（分析价格空间：价差 / β / 配腿权重）
    bool readPrevAdjPrice(DataContext& context, const String& symbol, double& out);

    /// 读 context 序列最近 n 个值（含可能出现的 NaN，调用方自行过滤）
    static bool readLastN(DataContext& context, const String& key, int32_t n, Vector<double>& out);

    /// 读最新价格：preferAdj=true 取 close（后复权），否则取 org_close
    static bool readLastPrice(DataContext& context, const String& symbol, bool preferAdj, double& out);

    /// 入场门槛：β 有效/区间/稳定性 + 价差趋势体制过滤。任一不满足返回 false。
    bool entryFiltersPass(DataContext& context, double betaRaw) const;

    double _entryZ = 2.0;      // 入场阈值
    double _exitZ = 0.5;       // 出场阈值
    double _stopZ = 3.5;       // 止损阈值
    int32_t _maxHoldBars = 0;  // 最长持有 bar 数（0 = 不启用）
    String _zField = "pair_z"; // 偏离度字段名（上游 FunctionNode 的输出名）

    // ── 配腿权重 / 入场过滤（全部可选，默认保持旧行为）──
    // notional_beta：strength = (1, β)，名义比 1:β（收益空间对冲）
    // price_spread ：strength = (P_A_hfq, β·P_B_hfq)，名义比 P_A:βP_B
    //                （价格水平价差对冲；名义按后复权价分摊 → 实际股数比
    //                 f_A : β·f_B，原始价 P&L 因而正比于 Δ(A_hfq - β·B_hfq)）
    String _weightMode = "notional_beta";
    double  _betaMin = 0.0;       // β 下限（0 = 不启用）
    double  _betaMax = 0.0;       // β 上限（0 = 不启用）
    double  _betaCvMax = 0.0;     // β 滚动变异系数上限（0 = 不启用）
    double  _regimeTrendMax = 0.0;  // 价差趋势上限（σ 倍数，0 = 不启用）
    int32_t _regimeTrendWindow = 20; // 趋势比较的回看 bar 数
    int32_t _betaCvWindow = 60;   // 计算 β CV 的回看 bar 数
    int32_t _regimeSigmaWindow = 40; // 趋势归一化用的价差 σ 窗口

    // 必须初始化：null_symbol() 是「腿未解析」的哨兵。未初始化时成员是未定义值，
    // Process() 里 `_legA == null_symbol()` 判不出来 → 解析腿的代码块整个被跳过，
    // 节点会拿野值当腿，永远读不到 z、永远不出信号。
    symbol_t _legA = null_symbol();  // 分子腿（pair_leg = +1）
    symbol_t _legB = null_symbol();  // 分母腿（pair_leg = -1)

    // 状态（跨 bar 维护，Prepare 时复位）
    int32_t _barsHeld = 0;   // 持有 bar 数
    bool _blocked: 1 = false;   // 止损后锁住再次入场
    bool _warnedNoLeg: 1 = false;
    bool _warnedNoShort: 1 = false;
    bool _warnedNoPrice: 1 = false;   // price_spread 模式下读不到价格只告警一次

    // ── 配对级交易统计（凯利的输入来源）────────────────────────────
    //
    // 统计单位是**一次往返**（一个配对仓位），不是单条腿：两条腿方向相反，
    // 各自胜率天然 ≈ 50%，腿级统计没有任何信息量。
    // 组合收益按 equity-unit 口径算（名义权重 w_A / w_B 在入场时确定）：
    //   π = w_A·dir_A·r_A + w_B·dir_B·r_B      （r 为原始价收益）
    //   notional_beta: w_A = 1/(1+β), w_B = β/(1+β)
    //   price_spread : w_A = P_A_hfq/(P_A_hfq+β·P_B_hfq), w_B = 1-w_A
    struct PairTradeStats {
        int    _trades = 0;
        int    _wins = 0;
        double _sum_win = 0.0;      // 盈利之和（正）
        double _sum_loss = 0.0;     // 亏损之和（正数）
    };
    PairTradeStats _stats;

    /// 当前这笔往返的入场信息（算出场收益要用）
    bool   _in_trade = false;
    double _entry_px_a = 0.0;      // 入场价（原始价，撮合口径）
    double _entry_px_b = 0.0;
    double _entry_w_a = 0.0;       // 入场时两腿的名义权重（权益占比）
    double _entry_w_b = 0.0;
    /// 分子腿 A 的入场方向（+1 多 / -1 空）。
    /// 配对两腿方向必然相反（只在 aBuy != bBuy 时记录），分母腿恒为 -_dir_a
    int    _dir_a = 0;
    double _trade_beta = 1.0;

    void recordTradeOpen(DataContext& context, double beta, int dirA);
    void recordTradeClose(DataContext& context);
    void publishTradeStats(DataContext& context, uint32_t groupId);
};
