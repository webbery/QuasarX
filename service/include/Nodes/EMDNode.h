#pragma once
#include "StrategyNode.h"

// ── EMD 内部耗时拆分（诊断用，非正式指标）──
// 只回答一个问题：EMD 节点的总耗时里，OMP 并行段 / 派生特征计算 / 串行发布
// 各占多少。与 GRAPH_PERF 共用一个编译开关：Release + ENABLE_GRAPH_PERF 时保留，
// 其它情况编译为空、零开销。诊断期结束后这三块代码连同成员整段删掉即可，
// 不影响任何业务逻辑。
#if defined(_DEBUG) || defined(ENABLE_GRAPH_PERF)
    #define EMD_PERF_SPLIT(...) __VA_ARGS__
#else
    #define EMD_PERF_SPLIT(...)
#endif

/**
 * EMD 经验模态分解节点（信号处理）
 *
 * 将非平稳时间序列分解为多个本征模态函数（IMF），
 * 适用于趋势提取、降噪和多尺度分析。
 *
 * 支持多种算法:
 *   - EMD: 标准 EMD（快速，适合实时）
 *   - CEEMDAN: 完备集合 EMD（高质量，抑制模态混叠）
 *
 * 输入：timeseries（如 close 价格序列）
 * 输出：label.IMF_0, label.IMF_1, ...（多个 IMF 分量时间序列）
 */

enum class EMDMethod {
    EMD,       // 标准 EMD
    CEEMDAN,   // 完备集合 EMD with Adaptive Noise
    VMD,       // 变分模态分解 (Variational Mode Decomposition)
};

class EMDNode : public QNode {
public:
    RegistClassName(EMDNode);
    EMDNode(Server* server);

    static const nlohmann::json getParams();

    virtual bool Init(const nlohmann::json& config) override;
    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;
    virtual Map<String, ArgType> out_elements() override;
    virtual void UpdateLabel(const String& label) override;

private:
    // 单个 symbol 的计算结果（并行区内只写自己的槽位，不碰 DataContext）
    //
    // 为什么必须这样拆：DataContext::add() 会经 AppendEpochValue 往
    // _lastWriteEpoch (std::map) 插入新 key，没有任何同步。EMD 的预热 NaN 占位
    // 写是这些 key 的首次 add，多线程并发 insert 会破坏红黑树，实测崩溃栈为
    // _Rb_tree_insert_and_rebalance / local_Rb_tree_rotate_left。
    // 因此并行区只产出结果，context 的 set/add 全部留到并行区之后串行做。
    struct SymbolOutput {
        String _prefix;                       // "{symbol}." ；空 = 未匹配到 symbol
        bool _warmup = false;                 // 预热期：按 NaN 占位路径发布
        bool _publish = false;                // 是否发布正式输出（分解失败/Skip 时不发布）
        // nimf 输出。滚动模式指向 _rollingIMFs 里的持久状态：每 epoch 只在尾部追加
        // 一个值，整段历史（~n × numIMFs）不必再拷一份，发布完下一 epoch 继续用。
        // 全局模式没有持久状态可指，持有本 epoch 的分解结果，发布时直接 move 进 context。
        const Vector<Vector<double>>* _imfs_view = nullptr;
        Vector<Vector<double>> _imfs_owned;
        bool _imfs_movable = false;           // true: _imfs_view 指向 _imfs_owned，可 move

        // 滚动模式的增量发布标志。
        //
        // 旧实现每 epoch 把 _rollingIMFs 里的整段历史拷进 context（O(n) 拷贝 +
        // 每 epoch 一次尺寸变大的 realloc），整轮下来是 O(n²)。改为：
        //   - bootstrap 那一次整段 set；
        //   - 之后每 epoch 只 add 本 epoch 新算出的那一个值。
        // 序列长度与内容与旧实现逐元素一致（长度始终等于已到 bar 数），
        // 但每 epoch 的写入成本从 O(n) 降到 O(1)。
        bool _imfs_incremental = false;        // true: 本 epoch 只新增 1 个值，走 add
        Vector<double> _imfs_new;              // 增量模式下每个 IMF 本 epoch 的新值

        Vector<double> _energy_velocity;      // 本 epoch 现算的派生特征，发布后不再需要 -> move
        Vector<double> _volume_regime;
        bool _has_volume_regime = false;      // false = 没算（无 volume 输入），保持空 key

        // 本 symbol 派生特征计算耗时。并行区内只写自己的槽位，无竞争。
        EMD_PERF_SPLIT(double _derivedMs = 0.0;)
    };

    // 对单个输入序列执行 EMD（全局或滚动）
    bool decomposeOne(const Vector<double>& input_data,
                      Vector<Vector<double>>& out_imfs) const;

    // 计算 EMD 衍生特征
    Vector<double> computeEnergyVelocity(const Vector<Vector<double>>& imfs, int window) const;
    Vector<double> computeVolumeRegime(const Vector<Vector<double>>& imfs,
                                       const Vector<double>& volume, int window) const;

    // Process 中每个 symbol 的主体 (提取出来便于 OMP 并行)
    //
    // 并行区内只计算、只读 context，结果写进调用方给的 SymbolOutput 槽位；
    // 真正的 context 写入由 Process 在并行区结束后串行执行（见 SymbolOutput）。
    NodeProcessResult processSymbol(const String& inputKey, DataContext& context,
                                    SymbolOutput& out);

    // 清空某个 symbol 的输出 key（分解失败 / 产出少于 numIMFs 时保持旧语义）
    //
    // 旧实现靠每 epoch 的预填把输出 key 重置成空 Vector，分解失败的 symbol
    // 下游读到的是空序列。预填去掉后由这里显式清一次（罕见路径，不在热路径）。
    //
    // @param fromImf   从第几个 IMF 开始清
    // @param clearDerived 是否连派生特征（energy_velocity / volume_regime）一起清
    void clearOutputs(DataContext& context, const String& prefix,
                      int fromImf, bool clearDerived);

    Server* _server;
    String _label;
    EMDMethod _method:3;             // 算法类型
    WarmupFillType _fillmode:2;
    bool _computeEnergyVelocity: 1;   // 是否计算 energy_velocity
    bool _computeVolumeRegime: 1;     // 是否计算 volume_regime
    int _numIMFs;                  // IMF 分量数量
    int _ensembles;                // CEEMDAN 集合数
    double _noiseStd;              // CEEMDAN 噪声强度
    double _alpha;                 // VMD 带宽惩罚参数
    double _tau;                   // VMD 对偶步长
    double _tol;                   // VMD 收敛阈值
    int _windowSize;               // 滚动窗口大小（0 = 全序列一次分解）
    String _volumeContextKey;      // 从实际连接解析出的 volume 输入 context key（如 "sz.002825.volume"）
    Set<String> _symbolPrefixes;   // 来自 discoverUpstreamSymbols() 的 symbol 列表（带 "." 后缀，如 "sz.002825."），用于 Process 中从 inputKey 匹配出正确 prefix
    Map<String, ArgType> _params;  // 输入参数（来自上游节点的输出）
    Map<String, ArgType> _outputs; // 输出元素声明

    // 滚动模式持久状态：避免每个 epoch 重复计算全部历史
    // key = inputKey, value = 累积的 IMF 输出向量
    Map<String, Vector<Vector<double>>> _rollingIMFs;

    // EMD 内部耗时累计（诊断用，见文件头的 EMD_PERF_SPLIT）。
    // Process 由单个策略 worker 线程调用，累计无需原子。
    EMD_PERF_SPLIT(
        double _tParallelMs = 0.0;   // OMP 并行段（含派生特征计算）
        double _tDerivedMs = 0.0;    // 其中派生特征计算的累计
        double _tPublishMs = 0.0;    // 串行发布段
    )
};
