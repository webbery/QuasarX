#pragma once
#include "StrategyNode.h"

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
        String prefix;                        // "{symbol}." ；空 = 未匹配到 symbol
        bool warmup = false;                  // 预热期：按 NaN 占位路径发布
        bool publish = false;                 // 是否发布正式输出（分解失败/Skip 时不发布）
        // nimf 输出。滚动模式指向 _rollingIMFs 里的持久状态：每 epoch 只在尾部追加
        // 一个值，整段历史（~n × numIMFs）不必再拷一份，发布完下一 epoch 继续用。
        // 全局模式没有持久状态可指，持有本 epoch 的分解结果，发布时直接 move 进 context。
        const Vector<Vector<double>>* imfs_view = nullptr;
        Vector<Vector<double>> imfs_owned;
        bool imfs_movable = false;            // true: imfs_view 指向 imfs_owned，可 move

        Vector<double> energy_velocity;       // 本 epoch 现算的派生特征，发布后不再需要 -> move
        Vector<double> volume_regime;
        bool has_volume_regime = false;       // false = 没算（无 volume 输入），保持预填空 key
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
};
