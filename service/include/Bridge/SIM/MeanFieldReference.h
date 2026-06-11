#pragma once
#include "std_header.h"
#include <Eigen/Dense>
#include <cstdint>
#include <string>
#include <filesystem>

/**
 * @brief 冻结参考分布（方案 B）
 *
 * 从历史 CSV 数据加载多个标的的行情，计算：
 * 1. 每个标的的历史均值和对数收益率均值
 * 2. 标的间的相关系数矩阵（用于 M-V SDE 的耦合项）
 * 3. 板块平均价格（mean field 项）
 *
 * 设计要点：
 * - 参考分布在仿真期间保持不变（frozen reference measure）
 * - 所有 M-V SDE 的 drift/diffusion 项中的 mean field 都引用此分布
 * - 支持按时间窗口筛选历史数据
 */
class MeanFieldReference {
public:
    MeanFieldReference();
    ~MeanFieldReference() = default;

    /**
     * @brief 从数据目录加载历史数据
     * @param dataDir 数据目录路径（如 service/build/data/A_hfq/）
     * @param symbols 要加载的标的列表
     * @return 成功加载的标的数量
     */
    int LoadFromDirectory(const String& dataDir, const Vector<String>& symbols);

    /**
     * @brief 从单个 CSV 文件加载历史数据
     * @param symbol 标的代码
     * @param csvPath CSV 文件路径
     * @return 是否成功
     */
    bool LoadFromCSV(const String& symbol, const String& csvPath);

    /**
     * @brief 检查是否已加载参考数据
     */
    bool IsLoaded() const { return !_symbols.empty(); }

    // === 查询接口 ===

    /// @brief 获取所有已加载的标的
    const Vector<String>& GetSymbols() const { return _symbols; }

    /// @brief 获取标的数量
    int GetSymbolCount() const { return static_cast<int>(_symbols.size()); }

    /// @brief 获取某标的的历史收盘价序列
    const Vector<double>& GetClosePrices(const String& symbol) const;

    /// @brief 获取某标的的历史对数收益率序列
    const Vector<double>& GetLogReturns(const String& symbol) const;

    /// @brief 获取某标的的历史均值（价格）
    double GetMeanPrice(const String& symbol) const;

    /// @brief 获取某标的的历史均值（对数收益率）
    double GetMeanLogReturn(const String& symbol) const;

    /// @brief 获取某标的的历史波动率（对数收益率的标准差）
    double GetVolatility(const String& symbol) const;

    /// @brief 获取市场平均价格（mean field 项）
    Eigen::VectorXd GetMarketMeanPrice() const;

    /// @brief 获取相关系数矩阵（N×N）
    const Eigen::MatrixXd& GetCorrelationMatrix() const;

    /// @brief 获取协方差矩阵（N×N）
    const Eigen::MatrixXd& GetCovarianceMatrix() const;

    /// @brief 获取 Cholesky 分解下三角矩阵（用于生成相关随机数）
    const Eigen::MatrixXd& GetCholeskyFactor() const;

    /// @brief 计算两个标的之间的相关系数
    double GetCorrelation(const String& symA, const String& symB) const;

    /// @brief 获取数据起始/结束日期索引
    int GetDataLength() const { return _dataLength; }

private:
    /// @brief 计算相关系数矩阵
    void ComputeCorrelationMatrix();

    /// @brief 计算 Cholesky 分解
    void ComputeCholesky();

    /// @brief 对齐不同标的的收益率序列（确保时间窗口一致）
    void AlignReturns();

    /// @brief 从 CSV 行解析价格数据
    bool ParseCSVLine(const String& line, double& close, double* volume = nullptr);

    Vector<String> _symbols;                        ///< 已加载的标的列表
    Map<String, Vector<double>> _closePrices;       ///< 每个标的的收盘价序列
    Map<String, Vector<double>> _logReturns;        ///< 每个标的的对数收益率序列
    Map<String, double> _meanPrices;                ///< 每个标的的历史均价
    Map<String, double> _meanLogReturns;            ///< 每个标的的均值对数收益率
    Map<String, double> _volatilities;              ///< 每个标的的历史波动率

    Eigen::MatrixXd _correlationMatrix;             ///< 相关系数矩阵 N×N
    Eigen::MatrixXd _covarianceMatrix;              ///< 协方差矩阵 N×N
    Eigen::MatrixXd _choleskyFactor;                ///< Cholesky 分解下三角

    int _dataLength;                                ///< 对齐后的数据长度
};
