#pragma once
#include "Bridge/SIM/MeanFieldReference.h"
#include "std_header.h"
#include <Eigen/Dense>
#include <random>
#include <memory>

/**
 * @brief 单标的单粒子的 SDE 路径
 *
 * 存储一条完整的仿真价格路径，用于后续分析和统计
 */
struct MVParticlePath {
    String _symbol;           ///< 标的代码
    int _particleId;          ///< 粒子 ID
    Vector<double> _prices;   ///< 价格序列
    Vector<double> _returns;  ///< 收益率序列

    double GetFinalPrice() const { return _prices.empty() ? 0.0 : _prices.back(); }
    double GetInitialPrice() const { return _prices.empty() ? 0.0 : _prices.front(); }
    int GetLength() const { return static_cast<int>(_prices.size()); }
};

/**
 * @brief M-V SDE 参数配置
 *
 * 控制标的价格过程的漂移和扩散行为
 */
struct MVSDEParams {
    // Drift 参数
    double _mu0 = 0.0;        ///< 基础漂移（历史平均收益率）
    double _mu1 = -0.01;      ///< 均值回归系数（负值 = 向参考分布回归）
    double _mu2 = 0.0;        ///< 验证损失信号敏感度（暂未使用，Phase 2）

    // Diffusion 参数
    double _sigma0 = 0.02;    ///< 基础波动率
    double _sigma1 = 0.5;     ///< 偏离参考分布的波动放大系数
    double _sigma2 = 1.0;     ///< Planner 增强强度（Phase 2 由 MVPlanner 动态控制）

    // 耦合参数
    double _rho = 0.3;        ///< 板块相关系数缩放因子

    // 初始条件
    double _S0 = 10.0;        ///< 初始价格

    // 仿真参数
    int _numSteps = 252;      ///< 仿真步数（交易日）
    double _dt = 1.0 / 252.0; ///< 时间步长（年化）
};

/**
 * @brief McKean-Vlasov 粒子系统
 *
 * 使用 Euler-Maruyama 离散化求解 M-V SDE：
 *
 *   dS_t^i = μ(t, S_t^i, S̄_hist)dt + σ(t, S_t^i, S̄_hist)dW_t^i + ρ·d⟨W^i, W̄⟩_t
 *
 * 其中：
 * - S̄_hist 来自 MeanFieldReference 的冻结参考分布
 * - ρ 来自历史相关系数矩阵
 * - W_t^i 通过 Cholesky 分解生成相关随机数
 *
 * 设计要点：
 * - N 个标的 × M 个粒子并行演化
 * - 每条粒子路径独立存储，支持后续分析
 * - 使用 Boost.Random 保证跨平台一致性
 */
class MVParticleSystem {
public:
    MVParticleSystem();
    ~MVParticleSystem() = default;

    /**
     * @brief 初始化粒子系统
     * @param reference 冻结参考分布（必须已加载）
     * @param numParticles 每个标的的粒子数
     * @param params SDE 参数（可选，使用默认值）
     * @return 是否成功
     */
    bool Initialize(MeanFieldReference& reference, int numParticles,
                    const MVSDEParams& params = MVSDEParams());

    /**
     * @brief 执行一步 Euler-Maruyama 离散化
     * @param step 当前步数（0-based）
     * @return 是否成功
     */
    bool Step(int step);

    /**
     * @brief 执行完整仿真（从 step 0 到 numSteps-1）
     * @return 是否成功
     */
    bool Simulate();

    /**
     * @brief 重置粒子状态（保留配置，清空路径数据）
     */
    void Reset();

    // === 查询接口 ===

    /// @brief 获取标的数量
    int GetSymbolCount() const { return static_cast<int>(_symbols.size()); }

    /// @brief 获取粒子数
    int GetParticleCount() const { return _numParticles; }

    /// @brief 获取某标的某粒子的路径
    const MVParticlePath* GetParticlePath(const String& symbol, int particleId) const;

    /// @brief 获取某标的的所有粒子路径
    Vector<const MVParticlePath*> GetParticlePaths(const String& symbol) const;

    /// @brief 获取某标的在指定时刻的粒子均值（price）
    double GetParticleMeanPrice(const String& symbol, int step) const;

    /// @brief 获取某标的在指定时刻的粒子方差
    double GetParticleVariance(const String& symbol, int step) const;

    /// @brief 获取某标的在指定时刻的粒子中位数
    double GetParticleMedianPrice(const String& symbol, int step) const;

    /// @brief 获取某标的在指定时刻的指定分位数
    double GetParticleQuantile(const String& symbol, int step, double q) const;

    /// @brief 获取参考分布
    const MeanFieldReference* GetReference() const { return _reference; }

    /// @brief 获取 SDE 参数
    const MVSDEParams& GetParams() const { return _params; }

    /// @brief 修改 SDE 参数（用于 Phase 2 的 planner 动态调整）
    MVSDEParams& GetMutableParams() { return _params; }

    /// @brief 获取仿真是否完成
    bool IsFinished() const { return _finished; }

private:
    /// @brief 计算漂移项 μ(t, S, S̄_hist)
    double ComputeDrift(int symbolIdx, double S, int step) const;

    /// @brief 计算扩散项 σ(t, S, S̄_hist)
    double ComputeDiffusion(int symbolIdx, double S) const;

    /// @brief 生成相关随机数（通过 Cholesky 分解）
    Eigen::VectorXd GenerateCorrelatedNoise();

    /// @brief 获取参考分布的均值价格
    double GetReferenceMeanPrice(int symbolIdx) const;

    MeanFieldReference* _reference;    ///< 冻结参考分布
    MVSDEParams _params;               ///< SDE 参数
    Vector<String> _symbols;           ///< 标的列表
    int _numParticles;                 ///< 每个标的的粒子数

    // 粒子路径存储: [symbolIdx][particleId] -> MVParticlePath
    Vector<Vector<MVParticlePath>> _paths;

    std::mt19937 _rng;                ///< 随机数生成器
    bool _finished;                    ///< 仿真是否完成
};
