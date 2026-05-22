#pragma once
#include "std_header.h"
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <vector>

/**
 * @brief 协方差矩阵计算结果
 */
struct CovarianceResult {
    Eigen::MatrixXd covMatrix;      // N×N 协方差矩阵
    Eigen::MatrixXd corrMatrix;     // N×N 相关系数矩阵
    int n_assets = 0;
    int n_observations = 0;         // 有效交易日数（对齐后）
    double minCorrelation = 0.0;    // 最小相关系数（非对角线）
    double maxCorrelation = 0.0;    // 最大相关系数（非对角线）
    int nearCollinearPairs = 0;     // |ρ| > 0.95 的配对数
};

/**
 * @brief 协方差质量评估报告
 */
struct CovarianceQualityReport {
    bool isPositiveDefinite = false;
    double conditionNumber = 0.0;   // κ = λ_max / λ_min
    enum QualityGrade { EXCELLENT, GOOD, FAIR, POOR } grade = POOR;

    std::string gradeString() const {
        switch (grade) {
            case EXCELLENT: return "EXCELLENT";
            case GOOD:      return "GOOD";
            case FAIR:      return "FAIR";
            case POOR:      return "POOR";
        }
        return "UNKNOWN";
    }
};

/**
 * @brief 计算多资产协方差矩阵
 * @param returns 每个资产的日收益率序列，returns[i] = 资产 i 的收益率 vector
 *                各资产长度可以不同，函数自动取交集长度
 * @return CovarianceResult，失败时 n_assets=0
 */
CovarianceResult compute_covariance(const std::vector<std::vector<double>>& returns);

/**
 * @brief 评估协方差矩阵质量
 * @param cov 协方差矩阵计算结果
 * @return CovarianceQualityReport
 */
CovarianceQualityReport evaluate_covariance_quality(const CovarianceResult& cov);
