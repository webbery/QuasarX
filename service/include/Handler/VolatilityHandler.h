#pragma once
#include "HttpHandler.h"
#include "Util/data.h"
#include "Util/finance.h"
#include "Util/perf.h"
#include "Metric/RiskMetric.h"
#include "Metric/Drawdown.h"
#include "Metric/Volatility.h"
#include "Algorithms/ARModel.h"
#include <Eigen/Dense>
#include <vector>
#include <string>
#include <map>

enum class ACFDecayMode {
    Exponential,    // 指数衰减 (GARCH/EGARCH)
    Hyperbolic,     // 双曲衰减 (FIGARCH/长记忆)
    Inconclusive    // 无法判定
};

struct ACFDecayAnalysis {
    // Ljung-Box 检验
    double _lb_statistic = 0;          // Q 统计量
    double _lb_pvalue = 0;             // p 值
    bool _has_autocorrelation = false; // p < 0.05 表示存在显著自相关

    // 衰减模式拟合
    double _exponential_r2 = 0;        // 指数衰减 R² (GARCH/EGARCH)
    double _hyperbolic_r2 = 0;         // 双曲衰减 R² (FIGARCH/长记忆)
    ACFDecayMode _decay_mode = ACFDecayMode::Inconclusive;
    double _decay_half_life = 0;       // 指数衰减半衰期
    double _hurst_estimate = 0;        // Hurst 指数估计
};

struct VolatilitySingleResult {
    // 单序列仍用 std::vector：下游 metric/finance 接口按 std::vector 取参，
    // 换成 Eigen 只会增加边界转换。
    std::vector<double> _prices;
    std::vector<double> _returns;
    std::map<int, std::vector<double>> _rolling_vol;  // window -> values
    std::vector<double> _upper_2sigma;
    std::vector<double> _mean_price;
    std::vector<double> _lower_2sigma;
    double _annual_volatility = 0;
    double _max_drawdown = 0;
    double _skewness = 0;
    double _kurtosis = 0;
    double _var_95 = 0;
    double _cvar_95 = 0;
    std::vector<double> _volumes;
    std::vector<double> _abs_returns;
    std::vector<double> _returns_acf;
    std::vector<double> _returns_pacf;
    std::vector<double> _abs_returns_acf;
    ACFDecayAnalysis _acf_decay;

    // AR(p) 预测结果
    ar_model::ForecastResult _forecast_returns;
    ar_model::ForecastResult _forecast_vol;
};

struct VolatilityMultiResult {
    // 矩阵和向量统一用 Eigen，特征分解/矩阵乘法/外推不再在嵌套 vector 间来回转换。
    Eigen::MatrixXd _correlation_matrix;
    Eigen::MatrixXd _covariance_matrix;
    Eigen::VectorXd _eigenvalues;       // 降序
    double _condition_number = 0;
    bool _is_positive_definite = true;
    Eigen::VectorXd _annual_volatility;
    size_t _num_observations = 0;       // T: 用于 MP 分布分析 (Q = T/N)

    // Marchenko-Pastur 谱指标滚动窗口分析
    finance::SpectrumIndicatorResult _spectrum_indicators;

    // Marchenko-Pastur 静态噪声谱分析（全样本）
    finance::MpNoiseResult _mp_noise;
    std::vector<std::string> _mp_symbols;  // 与 _mp_noise 的 per-asset share 对齐

    // 多资产预测外推
    struct MultiForecast {
        int _horizon = 0;
        std::vector<std::string> _symbols;
        Eigen::MatrixXd _forecast_cov;
        Eigen::MatrixXd _forecast_corr;
        Eigen::VectorXd _forecast_volatilities;
    };
    MultiForecast _multi_forecast;

    // === 多标的时间序列分析 ===
    struct LeadLagPair {
        std::string _symbol_x, _symbol_y;
        int _lead_lag;              // >0: y领先x, <0: x领先y
        double _max_correlation;    // 最大 |相关系数|
        std::vector<double> _ccf;   // 交叉相关系数序列
    };

    struct GrangerPair {
        std::string _from, _to;
        double _f_statistic;
        double _p_value;
        bool _is_significant;
        int _optimal_lag;
    };

    std::vector<LeadLagPair> _lead_lag_results;
    std::vector<GrangerPair> _granger_results;
};

struct VolatilityResult {
    std::vector<std::string> _symbols;
    std::vector<std::string> _dates;
    std::map<std::string, VolatilitySingleResult> _single;
    VolatilityMultiResult _multi;
};

/// 波动率接口的 action。拆分后每个 action 只计算自己需要的 section，
/// 避免客户端只取一项时仍然跑完整的 rolling spectrum / Granger / AR 外推。
enum class VolatilityAction : uint8_t {
    All = 0,               // 兼容旧行为：一次返回全部 section
    Single,                // 单标的时序指标
    Multi,                 // 相关/协方差矩阵 + 特征值质量
    Spectrum,              // MP 滚动窗口 m+/m- 谱指标
    Forecast,              // 多资产协方差外推
    LeadLag,               // 交叉相关 + 格兰杰因果
    MpNoise,               // MP 静态噪声谱 + 资产噪声占比 + 去噪
    CovarianceDiagnostics, // POST: 协方差矩阵质量诊断（旧默认）
    Unknown
};

inline const char* volatilityActionToString(VolatilityAction a) {
    switch (a) {
        case VolatilityAction::All:                   return "all";
        case VolatilityAction::Single:                return "single";
        case VolatilityAction::Multi:                 return "multi";
        case VolatilityAction::Spectrum:              return "spectrum";
        case VolatilityAction::Forecast:              return "forecast";
        case VolatilityAction::LeadLag:               return "lead_lag";
        case VolatilityAction::MpNoise:               return "mp_noise";
        case VolatilityAction::CovarianceDiagnostics: return "covariance_diagnostics";
        default:                                      return "unknown";
    }
}

inline VolatilityAction parseVolatilityAction(const std::string& s, bool allow_diagnostics = false) {
    if (s.empty() || s == "all" || s == "full" || s == "overview") {
        return VolatilityAction::All;
    }
    if (s == "single") return VolatilityAction::Single;
    if (s == "multi" || s == "moments" || s == "correlation") {
        return VolatilityAction::Multi;
    }
    if (s == "spectrum" || s == "mp_rolling" || s == "mp_spectrum") {
        return VolatilityAction::Spectrum;
    }
    if (s == "forecast") return VolatilityAction::Forecast;
    if (s == "lead_lag" || s == "leadlag" || s == "timeseries") {
        return VolatilityAction::LeadLag;
    }
    if (s == "mp_noise" || s == "mp" || s == "noise") {
        return VolatilityAction::MpNoise;
    }
    if (allow_diagnostics &&
        (s == "covariance_diagnostics" || s == "diagnostics")) {
        return VolatilityAction::CovarianceDiagnostics;
    }
    return VolatilityAction::Unknown;
}

struct VolatilityRequest {
    VolatilityAction _action = VolatilityAction::All;
    std::string _db_path;
    std::vector<std::string> _symbols;
    std::string _start_date;
    std::string _end_date;
    std::vector<int> _windows{20, 60, 120};
    String _price_field = "close";   // close/open/high/low/volume
    FillMethod _fill = FillMethod::None;
    int _band_window = 20;
    BarFreq _target_freq = BarFreq::Day;
    int _spectrum_window = 60;
    int _max_clusters = 10;       // mp_noise: n >= T 时聚类降维的目标簇数
    bool _include_matrix = false; // mp_noise: 是否返回相关矩阵/去噪矩阵
};

/// 每个 action 需要计算的 section。
struct VolatilitySections {
    bool _single = false;
    bool _multi = false;
    bool _spectrum = false;
    bool _forecast = false;
    bool _lead_lag = false;
    bool _mp_noise = false;

    static VolatilitySections forAction(VolatilityAction action) {
        VolatilitySections s;
        switch (action) {
            case VolatilityAction::All:
                s._single = s._multi = s._spectrum = true;
                s._forecast = s._lead_lag = s._mp_noise = true;
                break;
            case VolatilityAction::Single:   s._single = true; break;
            case VolatilityAction::Multi:    s._multi = true; break;
            case VolatilityAction::Spectrum: s._spectrum = true; break;
            case VolatilityAction::Forecast: s._forecast = true; break;
            case VolatilityAction::LeadLag:  s._lead_lag = true; break;
            case VolatilityAction::MpNoise:  s._mp_noise = true; break;
            default: break;
        }
        return s;
    }
};

class VolatilityHandler: public HttpHandler {
public:
    VolatilityHandler(Server* server): HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);
    virtual void post(const httplib::Request& req, httplib::Response& res);

private:
    // n×T 收益率矩阵（按标的请求顺序，尾部对齐）
    struct ReturnMatrix {
        Eigen::MatrixXd _values;
        std::vector<std::string> _symbols;
        size_t _n = 0;
        size_t _T = 0;
        bool valid() const { return _n >= 2 && _T >= 2; }
    };

    struct VolatilitySeries {
        std::vector<std::string> _dates;
        std::vector<double> _prices;
        std::vector<double> _volumes;
        std::vector<double> _returns;
    };

    struct VolatilityDataset {
        std::vector<std::string> _symbols;      // 成功加载，请求顺序
        std::vector<std::string> _common_dates; // 第一个成功标的的日期
        std::map<std::string, VolatilitySeries> _series;
    };

    // 请求解析
    bool parseRequest(const httplib::Request& req,
                      VolatilityRequest& out,
                      std::string& error) const;

    // 数据加载 / section 计算
    VolatilityDataset loadDataset(const VolatilityRequest& req,
                                  PerfStages& stages) const;
    ReturnMatrix buildReturnMatrix(const VolatilityDataset& dataset) const;
    VolatilityResult compute(const VolatilityRequest& req,
                             const VolatilitySections& sections,
                             PerfStages* perf_stages) const;

    VolatilitySingleResult computeSingle(const std::vector<double>& prices,
                                         const std::vector<double>& volumes,
                                         const std::vector<int>& windows,
                                         int band_window = 20) const;

    void computeMoments(const ReturnMatrix& rm,
                        VolatilityMultiResult& result,
                        PerfStages* perf_stages) const;
    void computeSpectrumSection(const ReturnMatrix& rm,
                                const std::vector<std::string>& dates,
                                int spectrum_window,
                                VolatilityMultiResult& result,
                                PerfStages* perf_stages) const;
    void computeForecastSection(const ReturnMatrix& rm,
                                VolatilityMultiResult& result,
                                PerfStages* perf_stages) const;
    void computeLeadLagSection(const ReturnMatrix& rm,
                               int max_lag,
                               VolatilityMultiResult& result,
                               PerfStages* perf_stages) const;
    void computeMpNoiseSection(const ReturnMatrix& rm,
                               int max_clusters,
                               VolatilityMultiResult& result,
                               PerfStages* perf_stages) const;

    std::vector<double> simpleReturns(const std::vector<double>& prices) const;
    double skewness(const std::vector<double>& data) const;
    double kurtosis(const std::vector<double>& data) const;
    ACFDecayAnalysis analyzeACFDecay(const std::vector<double>& abs_acf,
                                     const std::vector<double>& abs_returns) const;

    // JSON 序列化
    nlohmann::json singleToJson(const VolatilitySingleResult& sr) const;
    nlohmann::json multiMomentsToJson(const VolatilityMultiResult& multi) const;
    nlohmann::json spectrumToJson(
        const finance::SpectrumIndicatorResult& spectrum) const;
    nlohmann::json multiForecastToJson(
        const VolatilityMultiResult::MultiForecast& forecast) const;
    nlohmann::json timeSeriesToJson(const VolatilityMultiResult& multi) const;
    nlohmann::json mpNoiseToJson(const finance::MpNoiseResult& noise,
                                 const std::vector<std::string>& symbols,
                                 bool include_matrix) const;
    nlohmann::json matrixToJson(const Eigen::MatrixXd& matrix) const;
    nlohmann::json vectorToJson(const Eigen::VectorXd& vector) const;

    // POST
    void handleMpNoisePost(const nlohmann::json& params,
                           httplib::Response& res) const;
    void handleCovarianceDiagnosticsPost(const nlohmann::json& params,
                                         httplib::Response& res) const;

    // 协方差质量诊断: Eigen SelfAdjoint 特征值分解, 返回 (降序特征值, 条件数, 正定性)
    void evaluateCovarianceQuality(const Eigen::MatrixXd& cov,
                                   Eigen::VectorXd& eigenvalues_out,
                                   double& condition_number_out,
                                   bool& is_positive_definite_out) const;
};
