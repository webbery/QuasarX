#include "std_header.h"
#include "Metric/Covariance.h"
#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <cmath>
#include <algorithm>
#include <numeric>

// ============================================================
// 计算多资产协方差矩阵
// ============================================================

CovarianceResult compute_covariance(const std::vector<std::vector<double>>& returns) {
    CovarianceResult result;

    if (returns.empty() || returns[0].empty()) {
        return result;
    }

    int n = static_cast<int>(returns.size());  // 资产数
    // 取交集长度（各资产收益率序列的最小长度）
    int T = static_cast<int>(returns[0].size());
    for (int i = 1; i < n; i++) {
        T = std::min(T, static_cast<int>(returns[i].size()));
    }

    if (T < 2) {
        return result;  // 至少需要 2 个观测
    }

    result.n_assets = n;
    result.n_observations = T;

    // 构建 T×N 收益率矩阵（列 = 资产，行 = 交易日）
    Eigen::MatrixXd R(T, n);
    for (int j = 0; j < n; j++) {
        // 计算该资产的均值
        double mean = 0.0;
        for (int i = 0; i < T; i++) {
            mean += returns[j][i];
        }
        mean /= T;

        // 去均值后填入矩阵
        for (int i = 0; i < T; i++) {
            R(i, j) = returns[j][i] - mean;
        }
    }

    // 协方差矩阵: Σ = (1/(T-1)) * R^T * R
    result.covMatrix = (R.transpose() * R) / static_cast<double>(T - 1);

    // 相关系数矩阵: ρ_ij = Σ_ij / sqrt(Σ_ii * Σ_jj)
    result.corrMatrix.resize(n, n);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            if (i == j) {
                result.corrMatrix(i, j) = 1.0;
            } else {
                double vi = result.covMatrix(i, i);
                double vj = result.covMatrix(j, j);
                if (vi > 0.0 && vj > 0.0) {
                    result.corrMatrix(i, j) = result.covMatrix(i, j) / std::sqrt(vi * vj);
                } else {
                    result.corrMatrix(i, j) = 0.0;
                }
            }
        }
    }

    // 统计非对角线相关系数
    result.minCorrelation = 1.0;
    result.maxCorrelation = -1.0;
    result.nearCollinearPairs = 0;

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double rho = result.corrMatrix(i, j);
            result.minCorrelation = std::min(result.minCorrelation, rho);
            result.maxCorrelation = std::max(result.maxCorrelation, rho);
            if (std::abs(rho) > 0.95) {
                result.nearCollinearPairs++;
            }
        }
    }

    return result;
}

// ============================================================
// 评估协方差矩阵质量
// ============================================================

CovarianceQualityReport evaluate_covariance_quality(const CovarianceResult& cov) {
    CovarianceQualityReport report;

    if (cov.n_assets < 2 || cov.covMatrix.rows() == 0) {
        return report;
    }

    int n = cov.n_assets;

    // 使用 SelfAdjointEigenSolver 计算特征值（对称矩阵专用，高效）
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> solver(cov.covMatrix);
    if (solver.info() != Eigen::Success) {
        return report;  // 分解失败
    }

    const auto& eigenvalues = solver.eigenvalues();  // 已排序（升序）

    // 正定性检验：所有特征值 > 0
    report.isPositiveDefinite = eigenvalues.minCoeff() > 1e-12;

    // 条件数: κ = λ_max / λ_min
    double lambda_min = eigenvalues(0);
    double lambda_max = eigenvalues(n - 1);
    if (lambda_min > 1e-15) {
        report.conditionNumber = lambda_max / lambda_min;
    } else {
        report.conditionNumber = std::numeric_limits<double>::infinity();
    }

    // 质量等级
    if (report.conditionNumber < 100) {
        report.grade = CovarianceQualityReport::EXCELLENT;
    } else if (report.conditionNumber < 1000) {
        report.grade = CovarianceQualityReport::GOOD;
    } else if (report.conditionNumber < 10000) {
        report.grade = CovarianceQualityReport::FAIR;
    } else {
        report.grade = CovarianceQualityReport::POOR;
    }

    return report;
}
