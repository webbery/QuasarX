#pragma once
#include <random>
#include <vector>

enum VolatilityType {
    VT_Static,
    VT_ARCH,
    VT_GARCH,
};

class Volatility {
public:

};

class StaticVolatility : public Volatility {
public:
    StaticVolatility();

    double GetValue();
};

class EMWAVolatility {
public:
};

class ArchVolatility {
public:
    ArchVolatility(const std::vector<double>& r);

    double GetValue(int index);

private:

private:
    std::vector<double> _r;

    bool _correlate;
};
