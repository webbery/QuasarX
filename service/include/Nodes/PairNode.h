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
    /// 往 context 追加一个标量
    static void publish(DataContext& context, const String& symbol, const String& field,
                         double value);

    double _entryZ = 2.0;      // 入场阈值
    double _exitZ = 0.5;       // 出场阈值
    double _stopZ = 3.5;       // 止损阈值
    int32_t _maxHoldBars = 0;  // 最长持有 bar 数（0 = 不启用）
    String _zField = "pair_z"; // 偏离度字段名（上游 FunctionNode 的输出名）

    symbol_t _legA;  // 分子腿（pair_leg = +1）
    symbol_t _legB;  // 分母腿（pair_leg = -1）

    // 状态（跨 bar 维护，Prepare 时复位）
    int32_t _barsHeld = 0;   // 持有 bar 数
    bool _blocked = false;   // 止损后锁住再次入场
    bool _warnedNoLeg = false;
    bool _warnedNoShort = false;
};
