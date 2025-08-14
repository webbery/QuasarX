#include "Risk/RiskMetric.h"
#include <algorithm>
#include <cassert>
#include <boost/math/distributions.hpp>
#include <numeric>

RiskMetric::RiskMetric(float confidence, double freerate, const PortfolioInfo& portfolio, std::shared_ptr<DataGroup> group)
:_confidence(confidence), _freerate(freerate), _mean(0) {
    assert(portfolio._holds.size() > 0);
    double total = 0;
    for (auto& item: portfolio._holds) {
        auto& asset = item.second;
        total += asset._hold * asset._price;
    }
    if (total == 0)
        return;

    Eigen::MatrixXd m(portfolio._holds.size(), 1);
    int r = 0;
    List<String> symbols;
    for (auto& item: portfolio._holds) {
        auto& asset = item.second;
        auto weight = asset._hold * asset._price / total;
        m(++r, 1) = weight;
        symbols.emplace_back(asset._symbol);
    }
    _weight.swap(m);
    // _correlation = group->Correlation(symbols);
    r = 0;

    for (auto& symbol: symbols) {
        auto ret = group->Return(symbol, 21);
        double w = m(++r, 1);
        double sum = std::accumulate(ret.begin(), ret.end(), 0);
        _mean += w * sum/ret.size();
        _sigma.push_back(group->Sigma(symbol, 21));
    }
}

Vector<float> RiskMetric::ParametricVaR(int gap) {
    boost::math::normal_distribution<> norm(0, 1); // 均值0，标准差1
    double factor = quantile(norm, _confidence); // 分位数值
    Vector<float> result;
    for (auto sigma: _sigma) {
        double var = _mean + factor * sigma * sqrt(gap);
        result.push_back(var);
    }
    return result;
}

float RiskMetric::HistoricalVaR(short Ndays) {
    return 0;
}

float RiskMetric::MonteCarloVaR(uint32_t times) {

    return 0;
}

float RiskMetric::ExpectedShortfall() {

    return 0;
}