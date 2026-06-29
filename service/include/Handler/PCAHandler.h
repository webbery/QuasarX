#pragma once
#include "HttpHandler.h"
#include "Util/data.h"
#include "Util/finance.h"
#include <vector>
#include <string>
#include <map>

/// PCA 分析模式
enum class PCAMode {
    CrossSection,   // 截面 PCA：多标的共同因子分析
    TimeSeries      // 时序 PCA：单标的多特征降维
};

/// PCA 质量评估指标
struct PCAQualityMetrics {
    double kmo = 0;                    // KMO 检验统计量 (>0.6 适合)
    double bartlett_stat = 0;          // Bartlett 球形检验卡方值
    double bartlett_pvalue = 0;        // Bartlett p 值 (<0.05 显著)
    double cumulative_variance = 0;    // 累计方差解释率（前K个PC）
    double condition_number = 0;       // 条件数 κ
    bool is_positive_definite = false; // 是否正定
    double reconstruction_error = 0;   // 重构误差（Frobenius 范数）
    double reconstruction_error_pct = 0; // 重构误差占比 (%)

    // 评估等级
    String kmo_grade;      // "非常适合" / "适合" / "一般" / "不适合"
    String cond_grade;     // "优秀" / "良好" / "可接受" / "差"
    String variance_grade; // "优秀" / "良好" / "可接受" / "差"
};

/// 截面 PCA 结果（多标的）
struct PCACrossSectionResult {
    std::vector<std::string> symbols;
    std::vector<double> eigenvalues;           // 特征值（降序）
    std::vector<double> variance_ratio;        // 各 PC 方差解释比
    std::vector<double> cumulative_variance;   // 累计方差解释比
    std::vector<std::vector<double>> loadings; // 载荷矩阵 (n_symbols × n_components)
    std::vector<std::vector<double>> scores;   // 得分矩阵 (n_dates × n_components)
    std::vector<std::vector<double>> corr_original;  // 原始相关系数矩阵
    std::vector<std::vector<double>> corr_reconstructed; // 重构相关系数矩阵（前K个PC）
    int n_components;                          // 实际使用的 PC 数量
    int n_symbols;
    int n_observations;
    PCAQualityMetrics quality;                 // 质量评估
};

/// 时序 PCA 结果（单标的多特征）
struct PCATimeSeriesResult {
    std::string symbol;
    std::vector<std::string> features;         // 特征名称 [close, open, high, low, volume]
    std::vector<double> eigenvalues;
    std::vector<double> variance_ratio;
    std::vector<double> cumulative_variance;
    std::vector<std::vector<double>> loadings; // 载荷矩阵 (n_features × n_components)
    std::vector<std::vector<double>> scores;   // 得分矩阵 (n_dates × n_components)
    int n_components;
    int n_features;
    int n_observations;
    PCAQualityMetrics quality;                 // 质量评估
};

/// PCA 总体结果
struct PCAResult {
    PCAMode mode;
    std::vector<std::string> dates;
    PCACrossSectionResult cross_section;
    PCATimeSeriesResult time_series;
};

class PCAHandler : public HttpHandler {
public:
    PCAHandler(Server* server) : HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);

private:
    static PCACrossSectionResult computeCrossSection(
        const std::map<std::string, std::vector<double>>& returns_map,
        const std::vector<std::string>& symbols,
        int n_components);

    static PCATimeSeriesResult computeTimeSeries(
        const std::map<std::string, std::vector<double>>& features_map,
        const std::vector<std::string>& features,
        const std::string& symbol,
        int n_components);

    // 质量评估
    static PCAQualityMetrics computeQuality(
        const std::vector<std::vector<double>>& corr_matrix,
        const std::vector<double>& eigenvalues,
        const std::vector<double>& variance_ratio,
        const std::vector<double>& cumulative_variance,
        const std::vector<std::vector<double>>& corr_reconstructed,
        int n_components);
};
