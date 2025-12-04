#pragma once
#include "std_header.h"
#include "PortfolioSubsystem.h"

class IRiskMetric {
public:
    virtual double Evaluate() = 0;

};

class RiskMetric {
public:
    RiskMetric(float confidence, double freerate, const PortfolioInfo& portfolio);

    Vector<float> ParametricVaR(int gap = 10);

    float HistoricalVaR(short Ndays);

    float MonteCarloVaR(uint32_t times);

    float ExpectedShortfall();

    float StopLoss(float percent);
private:
    float _confidence;
    double _freerate;
    double _mean;
    Vector<double> _sigma;
    Eigen::MatrixXd _weight;
    Eigen::MatrixXd _correlation;
};