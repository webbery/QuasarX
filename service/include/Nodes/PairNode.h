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
 *   {sym}.pair_id     配对分组标识（两腿同值），下游据此做两腿原子下单
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
    /// 优先 org_close_w（不复权，与成交价同口径），退回 close（后复权）。
    static bool readPrevPrice(DataContext& context, const String& symbol, double& out);

    double _entryZ = 2.0;      // 入场阈值
    double _exitZ = 0.5;       // 出场阈值
    double _stopZ = 3.5;       // 止损阈值
    int32_t _maxHoldBars = 0;  // 最长持有 bar 数（0 = 不启用）
    String _zField = "pair_z"; // 偏离度字段名（上游 FunctionNode 的输出名）

    // 必须初始化：null_symbol() 是「腿未解析」的哨兵。未初始化时成员是未定义值，
    // Process() 里 `_legA == null_symbol()` 判不出来 → 解析腿的代码块整个被跳过，
    // 节点会拿野值当腿，永远读不到 z、永远不出信号。
    symbol_t _legA = null_symbol();  // 分子腿（pair_leg = +1）
    symbol_t _legB = null_symbol();  // 分母腿（pair_leg = -1)

    // 状态（跨 bar 维护，Prepare 时复位）
    int32_t _barsHeld = 0;   // 持有 bar 数
    bool _blocked = false;   // 止损后锁住再次入场
    bool _warnedNoLeg = false;
    bool _warnedNoShort = false;

    // ── 配对级交易统计（凯利的输入来源）────────────────────────────
    //
    // 统计单位是**一次往返**（一个配对仓位），不是单条腿：两条腿方向相反，
    // 各自胜率天然 ≈ 50%，腿级统计没有任何信息量。
    // 组合收益按 equity-unit 口径算（与 backtest_pair.py 的
    // not_a = E/(1+β)、not_b = E·β/(1+β) 同一口径）：
    //   π = (dir_A·r_A + β·dir_B·r_B) / (1 + β)
    struct PairTradeStats {
        int    _trades = 0;
        int    _wins = 0;
        double _sum_win = 0.0;      // 盈利之和（正）
        double _sum_loss = 0.0;     // 亏损之和（正数）
    };
    PairTradeStats _stats;

    /// 当前这笔往返的入场信息（算出场收益要用）
    bool   _in_trade = false;
    double _entry_px_a = 0.0;
    double _entry_px_b = 0.0;
    int    _dir_a = 0;              // +1 多 / -1 空
    int    _dir_b = 0;
    double _trade_beta = 1.0;

    void recordTradeOpen(DataContext& context, double beta, int dirA, int dirB);
    void recordTradeClose(DataContext& context);
    void publishTradeStats(DataContext& context, uint64_t pairId);
};
