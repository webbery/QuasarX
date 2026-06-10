#pragma once
#include "std_header.h"

/**
 * VMD — Variational Mode Decomposition（变分模态分解）
 *
 * 将信号分解为 K 个具有中心频率的本征模态函数（IMF）。
 * 相比 EMD/CEEMDAN：
 *   1. 非递归分解，无误差累积
 *   2. 频域闭合解，数学基础严格
 *   3. 中心频率 ω_k 直接给出周期信息
 *   4. 无需 ensembles，计算量确定
 *
 * 算法: ADMM（交替方向乘子法）在频域求解约束变分问题
 *   min Σ_k || ∂_t[(δ+j/πt)*u_k]·e^{-jω_k t} ||²  s.t. Σ u_k = f
 *
 * 使用自实现 radix-2 FFT（include/Algorithms/FFT.h）。
 */
class VMD {
public:
    /**
     * @brief VMD 配置参数
     */
    struct Config {
        int K = 5;                ///< IMF 数量
        double alpha = 2000.0;    ///< 带宽惩罚参数 (大=窄带, 小=宽带, 典型 500~5000)
        double tau = 0.0;         ///< 对偶上升步长 (噪声容忍, 信噪比高时为 0)
        double tol = 1e-6;        ///< 收敛阈值 (L2 相对误差)
        int maxIter = 200;        ///< 最大迭代次数
        bool symmetricPad = true; ///< 对称延拓 (处理端点效应)
    };

    /**
     * @brief 分解结果
     */
    struct Result {
        List<Vector<double>> imfs;     ///< IMF 分量列表 (时域)
        Vector<double> residual;        ///< 残差 (通常接近 0)
        Vector<double> centerFreqs;     ///< 中心频率 (归一化, 范围 [0, 0.5])
        int actualK = 0;                ///< 实际分解的 IMF 数
        int iterations = 0;             ///< 实际迭代次数
        double convergenceError = 0;    ///< 最终收敛误差
        bool converged = false;         ///< 是否收敛
    };

    VMD();

    /**
     * @brief 执行 VMD 分解
     * @param data  输入时间序列
     * @param cfg   配置参数
     * @return      分解结果
     */
    Result decompose(const Vector<double>& data, const Config& cfg);

    /**
     * @brief 获取运行时信息
     */
    String getSummary() const;

private:
    String _summary;

    /// 对称延拓到 2 的幂
    static Vector<double> symmetricPad(const Vector<double>& data, size_t& outSize);

    /// 从延拓结果中截取原始部分
    static Vector<double> unpad(const Vector<double>& padded, size_t origSize, size_t padLeft);
};
