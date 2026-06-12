#pragma once
#include "HttpHandler.h"
#include <vector>
#include <string>
#include <map>

struct VolatilitySingleResult {
    std::vector<double> prices;
    std::vector<double> returns;
    std::map<int, std::vector<double>> rolling_vol;  // window -> values
    std::vector<double> upper_2sigma;
    std::vector<double> upper_1sigma;
    std::vector<double> mean_price;
    std::vector<double> lower_1sigma;
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
};

struct VolatilityMultiResult {
    std::vector<std::vector<double>> correlation_matrix;
    std::vector<std::vector<double>> covariance_matrix;
    std::vector<double> eigenvalues;
    double condition_number = 0;
    bool is_positive_definite = true;
    std::vector<double> annual_volatility;
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
    static VolatilityResult compute(const std::vector<std::string>& symbols,
                                     const std::string& start_date,
                                     const std::string& end_date,
                                     const std::vector<int>& windows);

    static VolatilitySingleResult computeSingle(const std::vector<double>& prices,
                                                  const std::vector<double>& volumes,
                                                  const std::vector<int>& windows);

    static VolatilityMultiResult computeMulti(const std::map<std::string, std::vector<double>>& returns_map);

    static std::vector<double> loadPrices(const std::string& symbol,
                                           const std::string& start_date,
                                           const std::string& end_date,
                                           std::vector<std::string>& out_dates,
                                           std::vector<double>& out_volumes);

    static std::vector<double> simpleReturns(const std::vector<double>& prices);
    static double annualVolatility(const std::vector<double>& returns);
    static double maxDrawdown(const std::vector<double>& prices);
    static double skewness(const std::vector<double>& data);
    static double kurtosis(const std::vector<double>& data);
    static double computeVar(const std::vector<double>& returns, double confidence);
    static double computeCVaR(const std::vector<double>& returns, double confidence);
    static std::vector<double> rollingVol(const std::vector<double>& returns, int window);
    static std::vector<double> computeACF(const std::vector<double>& data, int max_lag);
    static std::vector<double> computePACF(const std::vector<double>& acf, int max_lag);
};
