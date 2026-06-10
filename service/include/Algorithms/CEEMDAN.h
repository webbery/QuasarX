#pragma once
#include "std_header.h"
#include <random>

/**
 * CEEMDAN — Complete Ensemble EMD with Adaptive Noise
 * (完备集合经验模态分解，自适应噪声)
 *
 * 将信号分解为多个 IMF 分量，相比标准 EMD：
 *   1. 有效抑制模态混叠 (Mode Mixing)
 *   2. 几乎无残余噪声
 *   3. 重建误差极低
 *
 * 算法:
 *   IMF_1 = mean( EMD_1(x + ε_0·w^i) )      for i=1..N
 *   IMF_k = mean( EMD_1(r_{k-1} + ε_{k-1}·EMD_k(w^i)) )
 *   r_k = r_{k-1} - IMF_k
 *
 * 使用 SIMD 加速包络、均值、减法、集合平均等热点操作。
 */
class CEEMDAN {
public:
    /**
     * @brief CEEMDAN 配置参数
     */
    struct Config {
        int numIMFs = 5;          ///< 期望的 IMF 分量数量
        int ensembles = 50;       ///< 集合数 N (噪声样本数量，越大越精确但越慢)
        double noiseStd = 0.2;    ///< 噪声标准差 (相对信号 std 的比例，典型 0.1~0.4)
        int maxSiftingIter = 10;  ///< 每轮筛选最大迭代次数
        double sdThreshold = 0.02;///< IMF 停止条件阈值 (均值包络/信号范围)
        uint64_t seed = 42;       ///< 随机种子 (设为 0 时使用 system_random，保证可复现)
    };

    /**
     * @brief 分解结果
     */
    struct Result {
        List<Vector<double>> imfs;     ///< IMF 分量列表
        Vector<double> residual;        ///< 最终残差 (趋势分量)
        int actualIMFs = 0;             ///< 实际生成的 IMF 数量
        double reconstructionError = 0; ///< 重建误差 (ΣIMF + residual - 原始信号 的 RMS)
    };

    CEEMDAN();

    /**
     * @brief 执行 CEEMDAN 分解
     * @param data  输入时间序列
     * @param cfg   配置参数 (使用默认值可获得良好结果)
     * @return      分解结果
     */
    Result decompose(const Vector<double>& data, const Config& cfg);

    /**
     * @brief 获取运行时信息（用于日志/调试）
     */
    String getSummary() const;

private:
    // ---- 内部状态 ----
    String _summary;

    // ---- 噪声生成 ----
    /// 生成 N 个高斯白噪声样本
    Vector<Vector<double>> generateNoiseEnsemble(size_t n, const Config& cfg);

    /// 计算信号的标准差
    static double computeStd(const Vector<double>& data);

    // ---- CEEMDAN 阶段函数 ----
    /// 阶段 1: IMF_1 = mean( EMD_1(x + ε_0·w^i) )
    Vector<double> computeFirstIMF(const Vector<double>& data,
                                    const Vector<Vector<double>>& noises,
                                    const Config& cfg);

    /// 阶段 k (k≥2): IMF_k = mean( EMD_1(r_{k-1} + ε_{k-1}·EMD_k(w^i)) )
    Vector<double> computeIMFk(const Vector<double>& residual,
                                int k,
                                const Vector<Vector<double>>& noises,
                                const Config& cfg);

    /// 对单个噪声信号执行 EMD 并提取第 k 个 IMF
    Vector<double> getNoiseIMF(const Vector<double>& noise, int k, const Config& cfg);

    /// 检查向量是否单调
    static bool isMonotonic(const Vector<double>& v);

    /// 计算重建误差 (RMS)
    static double computeReconstructionError(const Vector<double>& original,
                                              const Result& result);
};
