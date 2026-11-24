#include "Util/TimeSeries.h"
#include <algorithm>

namespace timeseries {

namespace {

/// Eigen 向量 → finance 内核使用的 std::vector
Vector<double> toStdVector(const Eigen::VectorXd& v) {
    return Vector<double>(v.data(), v.data() + v.size());
}

/// 取矩阵第 i 行（行 = 变量）
Eigen::VectorXd rowVector(const Eigen::MatrixXd& data, int i) {
    return data.row(i).transpose();
}

/// 合成的序号名（0, 1, 2 …），交给 finance 内核后可以原样解析回行下标
String indexName(size_t i) {
    return std::to_string(i);
}

size_t parseIndex(const String& name) {
    try {
        return (size_t)std::stoul(name);
    } catch (...) {
        return 0;
    }
}

/// finance::grangerCausalityTest 的 direction 是 "y_name→x_name"
/// 传序号名后形如 "1→0"，解析出来就是（原因, 结果）
GrangerPair fromCausality(const finance::GrangerCausalityResult& g) {
    GrangerPair p;
    size_t arrow = g._direction.find("→");
    if (arrow == String::npos) return p;

    p._effect_index = parseIndex(g._direction.substr(0, arrow));
    p._cause_index  = parseIndex(g._direction.substr(arrow + 3));
    p._f_statistic = g._f_statistic;
    p._p_value = g._p_value;
    p._is_significant = g._is_significant;
    p._optimal_lag = g._optimal_lag;
    return p;
}

} // namespace

UnitRoot ComputeUnitRoot(const Eigen::VectorXd& values) {
    UnitRoot r;
    auto values_vec = toStdVector(values);
    r._adf = finance::adfTestFull(values_vec, -1, "c");
    r._kpss = finance::kpssTest(values_vec, -1, "level");
    return r;
}

ResidualPair ComputeResiduals(const Eigen::VectorXd& x, const Eigen::VectorXd& y) {
    ResidualPair r;
    if (x.size() != y.size() || x.size() < 2) return r;

    auto ols = finance::olsRegression(toStdVector(x), toStdVector(y));
    r._residuals = Eigen::Map<const Eigen::VectorXd>(
        ols._residuals.data(), ols._residuals.size());
    return r;
}

finance::EGFullResult ComputeEngleGranger(const Eigen::VectorXd& x, const Eigen::VectorXd& y) {
    return finance::engleGrangerFull(toStdVector(x), toStdVector(y));
}

finance::JohansenResult ComputeJohansen(const Eigen::MatrixXd& data) {
    if (data.rows() < 2 || data.cols() == 0) return finance::JohansenResult{};
    return finance::johansenTest(data, 1, "const");
}

Vector<GrangerPair> ComputePairwiseGranger(const Eigen::MatrixXd& data, int max_lag) {
    Vector<GrangerPair> out;
    if (data.rows() < 2) return out;

    for (int i = 0; i < data.rows(); ++i) {
        for (int j = i + 1; j < data.rows(); ++j) {
            auto xi = toStdVector(rowVector(data, i));
            auto xj = toStdVector(rowVector(data, j));

            auto g1 = finance::grangerCausalityTest(
                xi, xj, max_lag, indexName(i), indexName(j));
            out.push_back(fromCausality(g1));

            auto g2 = finance::grangerCausalityTest(
                xj, xi, max_lag, indexName(j), indexName(i));
            out.push_back(fromCausality(g2));
        }
    }
    return out;
}

Vector<MultivariateGranger> ComputeMultivariateGranger(const Eigen::MatrixXd& data, int max_lag) {
    Vector<MultivariateGranger> out;
    if (data.rows() < 3) return out;

    Vector<String> names;
    names.reserve(data.rows());
    for (int i = 0; i < data.rows(); ++i) {
        names.push_back(indexName(i));
    }

    auto raw = finance::multivariateGrangerTest(data, names, max_lag);

    out.reserve(raw.size());
    for (const auto& r : raw) {
        MultivariateGranger m;
        m._cause_index = parseIndex(r._from);
        m._effect_index = parseIndex(r._to);
        m._wald_stat = r._wald_stat;
        m._p_value = r._p_value;
        m._optimal_lag = r._optimal_lag;
        m._is_significant = r._is_significant;
        for (const auto& c : r._condition_set) {
            m._condition_indices.push_back(parseIndex(c));
        }
        out.push_back(std::move(m));
    }
    return out;
}

} // namespace timeseries
