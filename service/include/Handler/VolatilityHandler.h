#pragma once
#include "HttpHandler.h"
#include "Util/data.h"
#include "Util/finance.h"
#include "Metric/RiskMetric.h"
#include "Metric/Drawdown.h"
#include "Metric/Volatility.h"
#include "Algorithms/ARModel.h"
#include <vector>
#include <string>
#include <map>

enum class PriceField {
    Close,
    Open,
    High,
    Low,
    Volume
};

enum class ACFDecayMode {
    Exponential,    // 指数衰减 (GARCH/EGARCH)
    Hyperbolic,     // 双曲衰减 (FIGARCH/长记忆)
    Inconclusive    // 无法判定
};

struct ACFDecayAnalysis {
    // Ljung-Box 检验
    double lb_statistic = 0;        // Q 统计量
    double lb_pvalue = 0;           // p 值
    bool has_autocorrelation = false; // p < 0.05 表示存在显著自相关

    // 衰减模式拟合
    double exponential_r2 = 0;      // 指数衰减 R² (GARCH/EGARCH)
    double hyperbolic_r2 = 0;       // 双曲衰减 R² (FIGARCH/长记忆)
    ACFDecayMode decay_mode = ACFDecayMode::Inconclusive;
    double decay_half_life = 0;     // 指数衰减半衰期
    double hurst_estimate = 0;      // Hurst 指数估计
};

struct VolatilitySingleResult {
    std::vector<double> prices;
    std::vector<double> returns;
    std::map<int, std::vector<double>> rolling_vol;  // window -> values
    std::vector<double> upper_2sigma;
    std::vector<double> mean_price;
    std::vector<double> lower_2sigma;
    double annual_volatility = 0;
    double max_drawdown = 0;
    double skewness = 0;
    double kurtosis = 0;
    double var_95 = 0;
    double cvar_95 = 0;
    std::vector<double> volumes;
    std::vector<double> abs_returns;
    std::vector<double> returns_acf;
    std::vector<double> returns_pacf;
    std::vector<double> abs_returns_acf;
    ACFDecayAnalysis acf_decay;

    // AR(p) 预测结果
    ar_model::ForecastResult forecast_returns;
    ar_model::ForecastResult forecast_vol;
};

struct VolatilityMultiResult {
    std::vector<std::vector<double>> correlation_matrix;
    std::vector<std::vector<double>> covariance_matrix;
    std::vector<double> eigenvalues;
    double condition_number = 0;
    bool is_positive_definite = true;
    std::vector<double> annual_volatility;

    // 多资产预测外推
    struct MultiForecast {
        int horizon = 0;
        std::vector<std::string> symbols;
        std::vector<std::vector<double>> forecast_cov;
        std::vector<std::vector<double>> forecast_corr;
        std::vector<double> forecast_volatilities;
    };
    MultiForecast multi_forecast;

    // === 多标的时间序列分析 ===
    struct LeadLagPair {
        std::string symbol_x, symbol_y;
        int lead_lag;              // >0: y领先x, <0: x领先y
        double max_correlation;    // 最大 |相关系数|
        std::vector<double> ccf;   // 交叉相关系数序列
    };

    struct GrangerPair {
        std::string from, to;
        double f_statistic;
        double p_value;
        bool is_significant;
        int optimal_lag;
    };

    struct CointegrationPair {
        std::string symbol_x, symbol_y;
        double beta, alpha;
        double adf_statistic;
        double p_value;
        bool is_cointegrated;
        double half_life;
    };

    std::vector<LeadLagPair> lead_lag_results;
    std::vector<GrangerPair> granger_results;
    std::vector<CointegrationPair> cointegration_results;
};

struct VolatilityResult {
    std::vector<std::string> symbols;
    std::vector<std::string> dates;
    std::map<std::string, VolatilitySingleResult> single;
    VolatilityMultiResult multi;
};

class VolatilityHandler: public HttpHandler {
public:
    VolatilityHandler(Server* server): HttpHandler(server) {}

    virtual void get(const httplib::Request& req, httplib::Response& res);

private:
    static VolatilityResult compute(const std::string& db_path,
                                     const std::vector<std::string>& symbols,
                                     const std::string& start_date,
                                     const std::string& end_date,
                                     const std::vector<int>& windows,
                                     PriceField field = PriceField::Close,
                                     FillMethod fill = FillMethod::None,
                                     int band_window = 20,
                                     BarFreq target_freq = BarFreq::Day);

    static VolatilitySingleResult computeSingle(const std::vector<double>& prices,
                                                  const std::vector<double>& volumes,
                                                  const std::vector<int>& windows,
                                                  int band_window = 20);

    static VolatilityMultiResult computeMulti(
        const std::map<std::string, std::vector<double>>& returns_map,
        const std::vector<std::string>& symbols,
        int max_lag = 10);

    static std::vector<double> simpleReturns(const std::vector<double>& prices);
    static double skewness(const std::vector<double>& data);
    static double kurtosis(const std::vector<double>& data);
    static ACFDecayAnalysis analyzeACFDecay(const std::vector<double>& abs_acf,
                                             const std::vector<double>& abs_returns);
};
