#include "Util/Volatility.h"
#include "boost/math/statistics/ljung_box.hpp"
using boost::math::statistics::ljung_box;

StaticVolatility::StaticVolatility(){

}

ArchVolatility::ArchVolatility(const std::vector<double>& r)
:_r(r) {
    auto [Q, P] = ljung_box(_r);
    if (P < 0.05) {
        _correlate = true;
    } else {
        _correlate = false;
    }
}
