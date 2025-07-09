#include "Util/finance.h"
#include "ql/models/marketmodels/correlations/expcorrelations.hpp"
#include <boost/math/statistics/univariate_statistics.hpp>
#include <ql/quantlib.hpp>

 using namespace QuantLib;
namespace finance {
double stage3GM(double g1, double g2, double D, double T1, double T2, double r) {
    if (r <= g2)
        return 0;

    Matrix a;
    return 0;
}

double sharp_ratio(double ret, double sigma, double free_rate) {
    return (ret - free_rate) / sigma;
}

}