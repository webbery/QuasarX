#include "Util/finance.h"
#include "Util/datetime.h"
#include <boost/math/statistics/univariate_statistics.hpp>
#include <filesystem>
#include "csv.h"
#include <cmath>

namespace finance {
double stage3GM(double g1, double g2, double D, double T1, double T2, double r) {
    if (r <= g2)
        return 0;

    return 0;
}

double kyles_lambda(const Vector<double>& prices,
                    const Vector<int64_t>& volumes,
                    int trade_side,
                    int64_t trade_volume) {
    size_t n = prices.size();
    if (n < 2 || volumes.size() != n) {
        return 0.0;
    }

    // 计算价格变化和订单流序列
    double sum_dp = 0, sum_of = 0, sum_dp_of = 0, sum_of2 = 0;
    size_t count = 0;

    for (size_t i = 1; i < n; ++i) {
        double dp = prices[i] - prices[i - 1];

        // 用价格变动方向代理订单流方向
        int direction = (dp > 0) ? 1 : (dp < 0) ? -1 : 0;
        if (direction == 0) continue;  // 跳过无变化的tick

        double of = direction * (double)volumes[i];

        sum_dp += dp;
        sum_of += of;
        sum_dp_of += dp * of;
        sum_of2 += of * of;
        count++;
    }

    // 加入本次交易
    int last_direction = (trade_side == 0) ? 1 : -1;
    double last_of = last_direction * (double)trade_volume;

    // 如果历史数据不足，至少用本次交易
    if (count == 0) {
        // 只有1个点无法计算协方差/方差
        return 0.0;
    }

    // 线性回归: lambda = Cov(dp, of) / Var(of)
    double mean_dp = sum_dp / count;
    double mean_of = sum_of / count;

    double cov_dp_of = (sum_dp_of / count) - mean_dp * mean_of;
    double var_of = (sum_of2 / count) - mean_of * mean_of;

    if (var_of < 1e-12) {
        return 0.0;  // 订单流方差为0，无法计算
    }

    return cov_dp_of / var_of;
}

double amihud_illiquidity(const Vector<double>& prices,
                          const Vector<int64_t>& volumes) {
    size_t n = prices.size();
    if (n < 2 || volumes.size() != n) {
        return 0.0;
    }

    double sum_amihud = 0;
    size_t count = 0;

    for (size_t i = 1; i < n; ++i) {
        if (volumes[i] == 0) continue;

        double ret = (prices[i] - prices[i - 1]) / prices[i - 1];
        sum_amihud += std::abs(ret) / (double)volumes[i];
        count++;
    }

    return count > 0 ? sum_amihud / count : 0.0;
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

// ──────────────────────────────────────────────────────────────────────
// 信号分析 / 时序分析工具函数
// ──────────────────────────────────────────────────────────────────────

Vector<double> finance::computeACF(const Vector<double>& data, int max_lag) {
    Vector<double> acf;
    if (data.empty()) return acf;

    int n = static_cast<int>(data.size());
    double mean = 0;
    for (auto v : data) mean += v;
    mean /= n;

    double var = 0;
    for (auto v : data) {
        double d = v - mean;
        var += d * d;
    }
    if (var < 1e-15) {
        acf.resize(max_lag + 1, 1.0);
        return acf;
    }

    for (int lag = 0; lag <= max_lag && lag < n; ++lag) {
        double cov = 0;
        for (int i = 0; i < n - lag; ++i) {
            cov += (data[i] - mean) * (data[i + lag] - mean);
        }
        acf.push_back(cov / var);
    }
    return acf;
}

Vector<double> finance::computePACF(const Vector<double>& acf, int max_lag) {
    Vector<double> pacf;
    int n = static_cast<int>(acf.size()) - 1;
    if (n < 0) return pacf;

    // Durbin-Levinson 算法
    Vector<Vector<double>> phi(n + 1, Vector<double>(n + 1, 0));

    pacf.push_back(1.0);  // lag 0
    if (n >= 1) {
        phi[1][1] = acf[1];
        pacf.push_back(phi[1][1]);
    }

    for (int k = 2; k <= n; ++k) {
        double num = acf[k];
        for (int j = 1; j < k; ++j) {
            num -= phi[k-1][j] * acf[k - j];
        }
        double den = 1.0;
        for (int j = 1; j < k; ++j) {
            den -= phi[k-1][j] * acf[j];
        }
        if (std::abs(den) < 1e-10) {
            phi[k][k] = 0;
        } else {
            phi[k][k] = num / den;
        }
        pacf.push_back(phi[k][k]);

        for (int j = 1; j < k; ++j) {
            phi[k][j] = phi[k-1][j] - phi[k][k] * phi[k-1][k-j];
        }
    }

    return pacf;
}

double finance::estimateMeanPeriod(const Vector<double>& data) {
    if (data.size() < 4) return 0;

    int max_lag = std::min(20, static_cast<int>(data.size()) / 4);
    auto acf = computeACF(data, max_lag);

    // 找第一个过零点（ACF 从正变为负）
    for (size_t i = 1; i < acf.size(); ++i) {
        if (acf[i] < 0) {
            return 2.0 * static_cast<double>(i);
        }
    }
    return 0;
}

double finance::computeEnergyPct(const Vector<double>& component,
                                  const Vector<double>& original) {
    if (component.empty() || original.empty()) return 0;

    double comp_mean = 0;
    for (auto v : component) comp_mean += v;
    comp_mean /= component.size();

    double comp_var = 0;
    for (auto v : component) {
        double d = v - comp_mean;
        comp_var += d * d;
    }
    comp_var /= component.size();

    double orig_mean = 0;
    for (auto v : original) orig_mean += v;
    orig_mean /= original.size();

    double orig_var = 0;
    for (auto v : original) {
        double d = v - orig_mean;
        orig_var += d * d;
    }
    orig_var /= original.size();

    if (orig_var < 1e-15) return 0;
    return comp_var / orig_var;
}
