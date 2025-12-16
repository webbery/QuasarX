#include "Util/finance.h"
#include "Util/datetime.h"
#include <boost/math/statistics/univariate_statistics.hpp>
#include <filesystem>
#include "csv.h"

namespace finance {
double stage3GM(double g1, double g2, double D, double T1, double T2, double r) {
    if (r <= g2)
        return 0;

    return 0;
}

}

bool LoadStockQuote(DataFrame& df, const String& path) {
    if (!std::filesystem::exists(path))
        return false;

    String datetime;
    double open, close, high, low, volumn, amount, price_volatility, change_percent, turnover_rate;

    Vector<String> sv;
    df.load_column("datetime", sv);
    Vector<double> dv;
    for (auto name : { "open", "close", "high","low", "volume", "turnover",
        }) {
        df.load_column(name, dv);
    }
    uint32_t index = 0;
    io::CSVReader<7> reader(path);
    // 日期,开盘,收盘,最高,最低,成交量,成交额,换手率
    reader.read_header(io::ignore_extra_column, "datetime", "open", "close", "high", "low", "volume", "turnover");
    while (reader.read_row(datetime, open, close, high, low, volumn, turnover_rate)) {
        auto t = FromStr(datetime);
        df.append_row(&index, std::make_pair("datetime", t), std::make_pair("open", open), std::make_pair("close", close),
            std::make_pair("high", high), std::make_pair("low", low), std::make_pair("volume", volumn),
            std::make_pair("turnover", turnover_rate)
        );
        ++index;
    }
    return true;
}
