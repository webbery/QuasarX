#pragma once
#include "StrategyNode.h"
#include "std_header.h"

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
    // 对单个输入序列执行 EMD（全局或滚动）
    bool decomposeOne(const Vector<double>& input_data,
                      Vector<Vector<double>>& out_imfs) const;

    // 计算 EMD 衍生特征
    Vector<double> computeEnergyVelocity(const Vector<Vector<double>>& imfs, int window) const;
    Vector<double> computeVolumeRegime(const Vector<Vector<double>>& imfs,
                                       const Vector<double>& volume, int window) const;

    Server* _server;
    String _label;
    EMDMethod _method;             // 算法类型
    int _numIMFs;                  // IMF 分量数量
    int _ensembles;                // CEEMDAN 集合数
    double _noiseStd;              // CEEMDAN 噪声强度
    double _alpha;                 // VMD 带宽惩罚参数
    double _tau;                   // VMD 对偶步长
    double _tol;                   // VMD 收敛阈值
    int _windowSize;               // 滚动窗口大小（0 = 全序列一次分解）
    bool _computeEnergyVelocity;   // 是否计算 energy_velocity
    bool _computeVolumeRegime;     // 是否计算 volume_regime
    Map<String, ArgType> _params;  // 输入参数（来自上游节点的输出）
    Map<String, ArgType> _outputs; // 输出元素声明
};
