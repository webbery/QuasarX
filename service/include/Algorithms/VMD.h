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
        double tol = 1e-6;        ///< 收敛阈值 (模态频谱平均变化 uDiff, 对齐 vmdpy)
        int maxIter = 500;        ///< 最大迭代次数 (对齐 vmdpy Niter)
        bool symmetricPad = true; ///< 对称延拓 (处理端点效应)
    };

    /**
     * @brief 分解结果
     */
    struct Result {
        Vector<Vector<double>> imfs;     ///< IMF 分量列表 (时域)
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
    /**
     * @brief SoA 布局的 VMD 状态（优化版）
     *
     * 将复数频谱从交错布局 [re0, im0, re1, im1, ...] 改为分离的实部/虚部平面，
     * 使内层循环变成纯 double* 运算，让编译器自动向量化（AVX-512: 8 doubles/指令）。
     *
     * 循环融合后不再需要 uHatOld 快照（旧值在寄存器内直接求差）与 residualHat
     * 中间缓冲（残差就地消费），每次迭代省下 K×2 个平面的写回/重读。
     */
    struct VMDState {
        Vector<Vector<double>> _uHat_re;  ///< [K][freqLen] 当前迭代 û_k 实部
        Vector<Vector<double>> _uHat_im;  ///< [K][freqLen] 当前迭代 û_k 虚部
        Vector<double> _uHatTotal_re;     ///< [freqLen] Σ_j û_j 实部（轮首刷新 + 增量维护）
        Vector<double> _uHatTotal_im;     ///< [freqLen] Σ_j û_j 虚部
        Vector<double> _fHat_re;          ///< [freqLen] 输入信号频谱实部
        Vector<double> _fHat_im;          ///< [freqLen] 输入信号频谱虚部
        Vector<double> _lambdaHat_re;     ///< [freqLen] 对偶变量实部
        Vector<double> _lambdaHat_im;     ///< [freqLen] 对偶变量虚部
        Vector<double> _omegaAxis;        ///< [freqLen] 归一化频率轴
        Vector<double> _epsAcc;           ///< [freqLen] 收敛量逐元素累积器 Σ_k |Δû_k|²
        Vector<double> _omega;            ///< [K] 中心频率

        /**
         * @brief 初始化状态
         * @param K IMF 数量
         * @param freqLen 频谱长度（fftSize/2 + 1）
         */
        void init(int K, size_t freqLen);
    };

    /// ADMM 迭代统计（对齐 vmdpy 收敛语义）
    struct IterStats {
        int _iterations = 0;   ///< 实际迭代次数
        double _eps = 1.0;     ///< 末轮收敛指标 uDiff（未进入第 2 轮时保持初值 1.0）
        bool _converged = false;  ///< eps < tol
    };

    String _summary;

    /// 镜像对称延拓到 2 的幂(关于端点 data[0]/data[n-1] 反射)
    static Vector<double> symmetricPad(const Vector<double>& data, size_t& outSize);

    /// 从延拓结果中截取原始部分
    static Vector<double> unpad(const Vector<double>& padded, size_t origSize, size_t padLeft);

    /**
     * @brief SoA 布局 + 循环融合的 VMD 核心迭代（ADMM）
     * @param state VMD 状态（SoA 布局）
     * @param cfg 配置参数
     * @param fftSize FFT 大小
     * @return 迭代统计（次数 / 收敛指标 / 是否收敛）
     */
    static IterStats decomposeSoA(VMDState& state, const Config& cfg, size_t fftSize);
};
