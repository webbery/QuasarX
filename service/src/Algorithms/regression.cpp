#include "Algorithms/regression.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <string>
#include <vector>

namespace regression {

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// ── 基础工具 ────────────────────────────────────────────────────────────────

inline double meanOf(const Vec& v) {
    return v.size() > 0 ? v.mean() : kNaN;
}

inline double sampleStd(const Vec& v) {
    const int n = static_cast<int>(v.size());
    if (n < 2) return 0.0;
    const double m = v.mean();
    return std::sqrt((v.array() - m).square().sum() / (n - 1));
}

inline bool isConstantColumn(const Vec& col) {
    if (col.size() == 0) return true;
    const double v0 = col(0);
    for (int i = 1; i < col.size(); ++i) {
        if (std::abs(col(i) - v0) > 1e-12) return false;
    }
    return true;
}

/// 一行是否可用：y 与该行全部 X 有限
inline bool rowValid(const Vec& y, const Mat& X, int i) {
    if (!std::isfinite(y(i))) return false;
    for (int j = 0; j < X.cols(); ++j) {
        if (!std::isfinite(X(i, j))) return false;
    }
    return true;
}

// ── 秩揭示 ──────────────────────────────────────────────────────────────────

/// 贪心选列：按原顺序逐列尝试，能带来新信息才保留。
///
/// 判据用"投影残差的相对范数"，而不是 QR 的主元阈值：
///   rel = ||A_j - P_kept(A_j)|| / ||A_j||
///   rel <= tol 视为线性相关。
/// 这样 C++（Eigen 求解）与 Python（numpy lstsq）用的是同一个可复现判据，
/// 不会因为两边 QR 实现的主元阈值不同而分叉。
///
/// 不直接用 QR 的主元顺序，是因为主元会打乱列序——截距列会被排到后面，
/// 坍缩掉"截距优先"这个业务语义。贪心保证：截距先入选，其余按调用方给的
/// 因子顺序依次判定，结果稳定可复现。
///
/// @param forcedDrop 非空时，这些列强制视为被剔除（横截面逐日求解时用于
///                   继承全样本的剔除决定，保证各日列集合可比）
void greedyRankSelect(const Mat& A, std::vector<int>& kept, std::vector<int>& dropped,
                      const std::vector<char>* forcedDrop = nullptr) {
    constexpr double kRankTol = 1e-10;
    constexpr double kZeroNorm = 1e-12;

    kept.clear();
    dropped.clear();
    if (A.rows() == 0 || A.cols() == 0) return;

    for (int j = 0; j < A.cols(); ++j) {
        if (forcedDrop != nullptr && (*forcedDrop)[j]) {
            dropped.push_back(j);
            continue;
        }

        const Vec col = A.col(j);
        const double norm = col.norm();
        if (!(norm > kZeroNorm)) {
            dropped.push_back(j);
            continue;
        }
        if (kept.empty()) {
            kept.push_back(j);
            continue;
        }

        Mat K(A.rows(), static_cast<int>(kept.size()));
        for (int m = 0; m < static_cast<int>(kept.size()); ++m) K.col(m) = A.col(kept[m]);

        const Vec coef = K.colPivHouseholderQr().solve(col);
        const double rel = (col - K * coef).norm() / norm;
        if (rel > kRankTol) {
            kept.push_back(j);
        } else {
            dropped.push_back(j);
        }
    }
}

/// 与已保留列中 |相关系数| 最大的那一列，作为剔除原因
std::string dropReason(const Mat& A, int droppedCol, const std::vector<int>& kept,
                       const std::vector<std::string>& names) {
    const Vec& x = A.col(droppedCol);
    const double sx = sampleStd(x);
    if (sx <= 1e-15) {
        return isConstantColumn(x) ? "constant" : "zero_variance";
    }
    double best = -1.0;
    std::string bestName = "unknown";
    for (int j : kept) {
        const Vec& z = A.col(j);
        const double sz = sampleStd(z);
        if (sz <= 1e-15) continue;
        const double mx = x.mean(), mz = z.mean();
        double cov = ((x.array() - mx) * (z.array() - mz)).sum();
        const double corr = std::abs(cov / (sx * sz * x.size()));
        if (corr > best) {
            best = corr;
            bestName = (j < static_cast<int>(names.size())) ? names[j] : ("col" + std::to_string(j));
        }
    }
    return "collinear_with:" + bestName;
}

// ── 求解器 ──────────────────────────────────────────────────────────────────

struct SolveResult {
    Vec              beta;     ///< 原列序（长度 k）；被剔除列为 NaN
    Vec              resid;    ///< 长度 n
    Mat              Akept;    ///< 保留列子矩阵（n×rank）
    Mat              XtXinv;   ///< 保留列的 (X'X)^-1（rank×rank）
    std::vector<int> kept;
    std::vector<int> dropped;
    int              rank = 0;
};

/// 求解保留列上的 OLS / 岭回归，并给出 (X'X)^-1
SolveResult solveDesign(const Mat& A, const Vec& b, double ridgeLambda,
                        const std::vector<char>* forcedDrop = nullptr) {
    SolveResult r;
    const int n = static_cast<int>(A.rows());

    greedyRankSelect(A, r.kept, r.dropped, forcedDrop);
    r.rank = static_cast<int>(r.kept.size());
    r.beta = Vec::Constant(A.cols(), kNaN);
    r.resid = Vec::Zero(n);
    if (r.rank == 0) return r;

    r.Akept.resize(n, r.rank);
    for (int m = 0; m < r.rank; ++m) r.Akept.col(m) = A.col(r.kept[m]);

    // 常量列（截距）不参与岭惩罚
    std::vector<char> isConst(r.rank, 0);
    for (int m = 0; m < r.rank; ++m) {
        isConst[m] = isConstantColumn(r.Akept.col(m)) ? 1 : 0;
    }

    const Mat XtX = r.Akept.transpose() * r.Akept;
    const Vec Xty = r.Akept.transpose() * b;

    Vec betaK;
    if (ridgeLambda > 0.0) {
        Mat XtXr = XtX;
        for (int m = 0; m < r.rank; ++m) {
            if (!isConst[m]) XtXr(m, m) += ridgeLambda;
        }
        Eigen::LDLT<Mat> ldlt(XtXr);
        betaK = ldlt.solve(Xty);
        r.XtXinv = ldlt.solve(Mat::Identity(r.rank, r.rank));
    } else {
        // 用 QR 求 beta（比正规方程稳），用 LDLT 求 (X'X)^-1
        Eigen::ColPivHouseholderQR<Mat> qr(r.Akept);
        betaK = qr.solve(b);
        Eigen::LDLT<Mat> ldlt(XtX);
        if (ldlt.info() == Eigen::Success) {
            r.XtXinv = ldlt.solve(Mat::Identity(r.rank, r.rank));
        } else {
            r.XtXinv = XtX.completeOrthogonalDecomposition().pseudoInverse();
        }
    }

    r.resid = b - r.Akept * betaK;
    for (int m = 0; m < r.rank; ++m) r.beta(r.kept[m]) = betaK(m);
    return r;
}

// ── 协方差 ──────────────────────────────────────────────────────────────────

/// HAC（Newey-West）三明治协方差
Mat covHAC(const Mat& A, const Vec& u, const Mat& XtXinv, int lag) {
    const int n = static_cast<int>(A.rows());
    const int k = static_cast<int>(A.cols());
    Mat omega = Mat::Zero(k, k);
    for (int t = 0; t < n; ++t) {
        const Vec xt = A.row(t).transpose();
        omega += (u(t) * u(t)) * (xt * xt.transpose());
    }
    int L = std::min(lag, n - 2);
    if (L < 0) L = 0;
    for (int j = 1; j <= L; ++j) {
        const double w = 1.0 - static_cast<double>(j) / static_cast<double>(L + 1);
        Mat gamma = Mat::Zero(k, k);
        for (int t = j; t < n; ++t) {
            const Vec xt = A.row(t).transpose();
            const Vec xtj = A.row(t - j).transpose();
            gamma += (u(t) * u(t - j)) * (xt * xtj.transpose());
        }
        omega += w * (gamma + gamma.transpose());
    }
    return XtXinv * omega * XtXinv;
}

/// 聚类稳健协方差（cluster 长度 n，与 A 的行对齐）
Mat covCluster(const Mat& A, const Vec& u, const Mat& XtXinv,
                const std::vector<int>& cluster, int rank) {
    const int n = static_cast<int>(A.rows());
    const int k = static_cast<int>(A.cols());
    std::map<int, std::vector<int>> groups;
    for (int t = 0; t < n && t < static_cast<int>(cluster.size()); ++t) {
        groups[cluster[t]].push_back(t);
    }    if (groups.size() < 2) return XtXinv;  // 无法聚类，退化

    Mat meat = Mat::Zero(k, k);
    for (const auto& [gid, idx] : groups) {
        Vec s = Vec::Zero(k);
        for (int t : idx) s += u(t) * A.row(t).transpose();
        meat += s * s.transpose();
    }
    const double G = static_cast<double>(groups.size());
    const double df = static_cast<double>(std::max(1, n - rank));
    const double c = (G / (G - 1.0)) * (static_cast<double>(n) - 1.0) / df;
    return c * (XtXinv * meat * XtXinv);
}

// ── 设计矩阵诊断 ────────────────────────────────────────────────────────────

/// 列标准化后的条件数（只做缩放，不改变列空间）
double computeCond(const Mat& A) {
    if (A.cols() < 2 || A.rows() < 2) return 0.0;
    Mat S = A;
    for (int j = 0; j < S.cols(); ++j) {
        const double nrm = S.col(j).norm();
        if (nrm > 1e-12) S.col(j) /= nrm;
    }
    Eigen::JacobiSVD<Mat> svd(S);
    const Vec sv = svd.singularValues();
    const double smin = sv(sv.size() - 1);
    return smin > 0.0 ? sv(0) / smin : kInf;
}

/// 方差膨胀因子（与 A 的列对齐；常量列与被剔除列留 0）
///
/// 注意：这里对每个 j 只做一次 QR，不走 solveDesign 的贪心选列——
/// 否则 K 列会退化成 O(K^2) 次 QR，全样本面板（T×N 可达数万行）直接卡住。
/// 共线列的 R² 会贴着 1，VIF 报 inf，语义上仍然正确。
std::vector<double> computeVIF(const Mat& A) {
    const int k = static_cast<int>(A.cols());
    const int n = static_cast<int>(A.rows());
    std::vector<double> vif(k, 0.0);
    if (k < 2 || n < 3) return vif;

    // 设计里已经有常量列（截距）就不再补——否则 Xo 秩亏，
    // QR 解与 Python 的 lstsq 解在秩亏时的投影会有微小差异，VIF 随之分叉。
    bool hasConst = false;
    for (int m = 0; m < k; ++m) {
        if (isConstantColumn(A.col(m))) { hasConst = true; break; }
    }

    for (int j = 0; j < k; ++j) {
        const Vec& yj = A.col(j);
        if (isConstantColumn(yj)) continue;

        Mat Xo(n, k - 1 + (hasConst ? 0 : 1));
        int c = 0;
        for (int m = 0; m < k; ++m) {
            if (m != j) Xo.col(c++) = A.col(m);
        }
        if (!hasConst) Xo.col(c).setOnes();

        Eigen::ColPivHouseholderQR<Mat> qr(Xo);
        const Vec fitted = Xo * qr.solve(yj);
        const double sst = (yj.array() - yj.mean()).square().sum();
        if (sst <= 1e-15) continue;
        const double r2 = 1.0 - (yj - fitted).squaredNorm() / sst;
        vif[j] = (r2 >= 1.0 - 1e-12) ? kInf : 1.0 / (1.0 - r2);
    }
    return vif;
}

// ── 统计分布 ────────────────────────────────────────────────────────────────

/// 正则化不完全 Beta 的连分式（Lentz 法，Numerical Recipes betacf）
double betacf(double a, double b, double x) {
    const int    kMaxIter = 200;
    const double kEps     = 3e-16;
    const double kFpMin   = 1e-300;

    const double qab = a + b;
    const double qap = a + 1.0;
    const double qam = a - 1.0;
    double c = 1.0;
    double d = 1.0 - qab * x / qap;
    if (std::abs(d) < kFpMin) d = kFpMin;
    d = 1.0 / d;
    double h = d;

    for (int m = 1; m <= kMaxIter; ++m) {
        const int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1.0 + aa * d;
        if (std::abs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::abs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        h *= d * c;

        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1.0 + aa * d;
        if (std::abs(d) < kFpMin) d = kFpMin;
        c = 1.0 + aa / c;
        if (std::abs(c) < kFpMin) c = kFpMin;
        d = 1.0 / d;
        const double del = d * c;
        h *= del;
        if (std::abs(del - 1.0) < kEps) break;
    }
    return h;
}

// ── 系数序列的均值检验 ──────────────────────────────────────────────────────

struct MeanTest {
    double _mean = kNaN;
    double _se   = kNaN;
    double _t    = kNaN;
    double _p    = kNaN;
    int    _n    = 0;
};

/// 对一串系数做均值检验：Var(mean) = S/n，S 为 Bartlett 加权长期方差
MeanTest seriesMeanTest(const std::vector<double>& values, SeType se, int hacLag) {
    MeanTest r;
    std::vector<double> v;
    v.reserve(values.size());
    for (double x : values) {
        if (std::isfinite(x)) v.push_back(x);
    }
    r._n = static_cast<int>(v.size());
    if (r._n == 0) return r;
    if (r._n == 1) {
        r._mean = v[0];
        return r;
    }

    const int n = r._n;
    double m = 0.0;
    for (double x : v) m += x;
    m /= n;
    r._mean = m;

    std::vector<double> d(n);
    for (int i = 0; i < n; ++i) d[i] = v[i] - m;

    double gamma0 = 0.0;
    for (double x : d) gamma0 += x * x;
    gamma0 /= n;

    double S = gamma0;
    int L = (se == SeType::HAC) ? std::min(hacLag, n - 2) : 0;
    if (L < 0) L = 0;
    for (int j = 1; j <= L; ++j) {
        const double w = 1.0 - static_cast<double>(j) / static_cast<double>(L + 1);
        double g = 0.0;
        for (int t = j; t < n; ++t) g += d[t] * d[t - j];
        g /= n;
        S += 2.0 * w * g;
    }
    if (S < 0.0) S = 0.0;

    const double var = S / n;
    r._se = std::sqrt(var);
    r._t = r._se > 0.0 ? m / r._se : kNaN;
    r._p = studentTTwoSidedP(std::abs(r._t), n - 1);
    return r;
}

}  // namespace

// ════════════════════════════════════════════════════════════════════════════
// 工具
// ════════════════════════════════════════════════════════════════════════════

double regularizedIncompleteBeta(double a, double b, double x) {
    if (!(a > 0.0) || !(b > 0.0)) return kNaN;
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;

    const double lbeta = std::lgamma(a) + std::lgamma(b) - std::lgamma(a + b);
    const double front = std::exp(a * std::log(x) + b * std::log1p(-x) - lbeta);
    if (x < (a + 1.0) / (a + b + 2.0)) {
        return front * betacf(a, b, x) / a;
    }
    return 1.0 - std::exp(b * std::log1p(-x) + a * std::log(x) - lbeta) *
                     betacf(b, a, 1.0 - x) / b;
}

double studentTTwoSidedP(double t, double df) {
    if (!std::isfinite(t) || !(df > 0.0)) return kNaN;
    if (t == 0.0) return 1.0;
    const double x = df / (df + t * t);
    return regularizedIncompleteBeta(df / 2.0, 0.5, x);
}

std::vector<int> buildGroupCodes(const std::vector<std::string>& labels, int minSize,
                                 const std::string& fallback,
                                 std::vector<std::string>& outLabels) {
    outLabels.clear();
    if (labels.empty()) return {};

    std::map<std::string, int> count;
    for (const auto& l : labels) count[l]++;

    std::vector<std::string> mapped(labels.size());
    for (size_t i = 0; i < labels.size(); ++i) {
        mapped[i] = (count[labels[i]] >= minSize) ? labels[i] : fallback;
    }

    std::set<std::string> levelSet(mapped.begin(), mapped.end());
    outLabels.assign(levelSet.begin(), levelSet.end());

    std::map<std::string, int> index;
    for (int k = 0; k < static_cast<int>(outLabels.size()); ++k) index[outLabels[k]] = k;

    std::vector<int> codes(labels.size(), 0);
    for (size_t i = 0; i < mapped.size(); ++i) codes[i] = index[mapped[i]];
    return codes;
}

void winsorizeCrossSection(Vec& values, double q) {
    if (!(q > 0.0) || q >= 0.5) return;
    std::vector<double> vals;
    vals.reserve(values.size());
    for (int i = 0; i < values.size(); ++i) {
        if (std::isfinite(values(i))) vals.push_back(values(i));
    }
    if (vals.size() < 3) return;
    std::sort(vals.begin(), vals.end());

    auto quantile = [&](double p) {
        const double pos = p * (static_cast<double>(vals.size()) - 1.0);
        const size_t lo = static_cast<size_t>(std::floor(pos));
        const size_t hi = static_cast<size_t>(std::ceil(pos));
        const double frac = pos - static_cast<double>(lo);
        return vals[lo] + frac * (vals[hi] - vals[lo]);
    };
    const double lo = quantile(q);
    const double hi = quantile(1.0 - q);
    for (int i = 0; i < values.size(); ++i) {
        if (!std::isfinite(values(i))) continue;
        values(i) = std::min(std::max(values(i), lo), hi);
    }
}

// ════════════════════════════════════════════════════════════════════════════
// 单次回归
// ════════════════════════════════════════════════════════════════════════════

Fit ols(const Vec& y, const Mat& X, const std::vector<std::string>& names,
        const FitOptions& opt, SeType se, const std::vector<int>& cluster) {
    Fit fit;
    const int nRaw = static_cast<int>(y.size());
    const int k = static_cast<int>(X.cols());
    if (nRaw <= 0 || k <= 0 || X.rows() != nRaw) {
        fit._warnings.push_back("输入为空或维度不一致");
        return fit;
    }
    fit._n_reg = k;

    // listwise 删除
    std::vector<int> rows;
    rows.reserve(nRaw);
    for (int i = 0; i < nRaw; ++i) {
        if (rowValid(y, X, i)) rows.push_back(i);
    }
    fit._n_dropped_rows = nRaw - static_cast<int>(rows.size());
    const int n = static_cast<int>(rows.size());
    fit._n_obs = n;
    if (n < 2) {
        fit._warnings.push_back("有效样本不足");
        return fit;
    }

    Mat A(n, k);
    Vec b(n);
    for (int r = 0; r < n; ++r) {
        const int i = rows[r];
        b(r) = y(i);
        for (int j = 0; j < k; ++j) A(r, j) = X(i, j);
    }

    SolveResult s = solveDesign(A, b, opt._ridge_lambda);
    fit._rank = s.rank;

    fit._terms.resize(k);
    for (int j = 0; j < k; ++j) {
        fit._terms[j]._name =
            (j < static_cast<int>(names.size())) ? names[j] : ("x" + std::to_string(j));
    }
    for (int j : s.dropped) {
        Term& t = fit._terms[j];
        t._dropped = true;
        t._drop_reason = dropReason(A, j, s.kept, names);
        // 未估计的项一律 NaN，不保留 Term 的默认值 0/1——
        // Python 侧输出 null，两边必须同形。
        t._beta = kNaN;
        t._se = kNaN;
        t._t = kNaN;
        t._p = kNaN;
    }
    if (s.rank == 0) {
        fit._warnings.push_back("设计矩阵秩为 0");
        return fit;
    }

    // 残差统计
    const double sse = s.resid.squaredNorm();
    const int df = n - s.rank;
    const double sigma2 = df > 0 ? sse / df : kNaN;
    fit._sigma = std::sqrt(sigma2);

    const double my = b.mean();
    const double sst = (b.array() - my).square().sum();
    fit._r2 = (sst > 1e-15) ? 1.0 - sse / sst : kNaN;
    fit._adj_r2 = (df > 0 && std::isfinite(fit._r2))
                      ? 1.0 - (1.0 - fit._r2) * (n - 1.0) / df
                      : kNaN;

    if (opt._time_ordered && n > 1) {
        double num = 0.0;
        for (int i = 1; i < n; ++i) {
            const double d = s.resid(i) - s.resid(i - 1);
            num += d * d;
        }
        fit._dw = sse > 1e-15 ? num / sse : kNaN;
    }

    // 协方差
    Mat V;
    if (se == SeType::HAC) {
        V = covHAC(s.Akept, s.resid, s.XtXinv, opt._hac_lag);
    } else if (se == SeType::Cluster && static_cast<int>(cluster.size()) == nRaw) {
        std::vector<int> c(n);
        for (int r = 0; r < n; ++r) c[r] = cluster[rows[r]];
        V = covCluster(s.Akept, s.resid, s.XtXinv, c, s.rank);
    } else {
        if (se == SeType::Cluster) {
            fit._warnings.push_back("聚类 id 长度不符，退化为 IID");
        }
        V = s.XtXinv * sigma2;
    }

    const double sy = sampleStd(b);
    for (int m = 0; m < s.rank; ++m) {
        const int j = s.kept[m];
        Term& t = fit._terms[j];
        t._beta = s.beta(j);
        t._se = std::sqrt(std::max(0.0, V(m, m)));
        t._t = t._se > 0.0 ? t._beta / t._se : kNaN;
        t._p = studentTTwoSidedP(std::abs(t._t), df);
        const Vec& xj = s.Akept.col(m);
        t._std_beta = (sy > 1e-15 && !isConstantColumn(xj))
                          ? t._beta * sampleStd(xj) / sy
                          : 0.0;
    }

    fit._cond = computeCond(s.Akept);
    fit._vif.assign(k, 0.0);
    if (opt._report_vif) {
        const std::vector<double> vifKept = computeVIF(s.Akept);
        for (int m = 0; m < s.rank; ++m) fit._vif[s.kept[m]] = vifKept[m];
    }
    return fit;
}

// ════════════════════════════════════════════════════════════════════════════
// 横截面回归
// ════════════════════════════════════════════════════════════════════════════

CrossSectionResult crossSection(const PanelData& p,
                                const std::vector<std::string>& xnames,
                                const std::vector<std::string>& fnames,
                                const std::vector<std::string>& groupnames,
                                const FitOptions& opt, SeType se) {
    CrossSectionResult out;
    const int T = static_cast<int>(p._dates.size());
    const int N = static_cast<int>(p._symbols.size());
    out._dates = p._dates;

    if (T == 0 || N == 0 || p._Y.rows() != T || p._Y.cols() != N) {
        out._warnings.push_back("面板维度不一致");
        return out;
    }
    // se 降级提示：逐日横截面回归没有聚类维度，cluster 退化为 iid。
    // 不静默——调用方必须知道拿到的不是自己请求的推断口径。
    if (se == SeType::Cluster) {
        out._warnings.push_back("cross_section 模式不支持 cluster 标准误，已退化为 iid");
    }

    // ── 1. 解析并固定列定义 ──
    struct Column {
        enum class Kind { Const, XPanel, FSeries, Dummy } _kind;
        std::string _name;
        int         _idx   = -1;  // xPanels / fSeries / groupDefs 的下标
        int         _level = -1;  // Dummy 的组号
    };

    std::vector<const Mat*> xPanels;
    std::vector<const Vec*> fSeries;

    struct GroupDef {
        std::vector<int>         _codes;   // 每个 symbol 的组号
        std::vector<std::string> _labels;  // 组号 -> 标签
    };
    std::vector<GroupDef> groupDefs;

    std::vector<Column> cols;
    std::vector<std::string> names;

    if (opt._add_const) {
        cols.push_back({Column::Kind::Const, "const", -1, -1});
    }

    for (const auto& xn : xnames) {
        auto it = p._X.find(xn);
        if (it == p._X.end() || it->second.rows() != T || it->second.cols() != N) {
            out._warnings.push_back("截面因子缺失或维度不符: " + xn);
            continue;
        }
        cols.push_back({Column::Kind::XPanel, xn, static_cast<int>(xPanels.size()), -1});
        xPanels.push_back(&it->second);
    }

    for (const auto& fn : fnames) {
        auto it = p._F.find(fn);
        if (it == p._F.end() || it->second.size() != T) {
            out._warnings.push_back("时序因子缺失或维度不符: " + fn);
            continue;
        }
        cols.push_back({Column::Kind::FSeries, fn, static_cast<int>(fSeries.size()), -1});
        fSeries.push_back(&it->second);
    }

    for (const auto& gn : groupnames) {
        auto it = p._groups.find(gn);
        if (it == p._groups.end() || static_cast<int>(it->second.size()) != N) {
            out._warnings.push_back("分组变量缺失或维度不符: " + gn);
            continue;
        }
        GroupDef gd;
        gd._codes = buildGroupCodes(it->second, opt._min_group_size, opt._fallback_group,
                                    gd._labels);
        if (gd._labels.size() < 2) {
            out._warnings.push_back("分组变量有效水平不足: " + gn);
            continue;
        }
        const int gidx = static_cast<int>(groupDefs.size());
        groupDefs.push_back(std::move(gd));

        // 参照组编码：加截距时丢掉排序最前的一级
        const int start = opt._add_const ? 1 : 0;
        for (int l = start; l < static_cast<int>(groupDefs[gidx]._labels.size()); ++l) {
            cols.push_back({Column::Kind::Dummy,
                            gn + "[" + groupDefs[gidx]._labels[l] + "]", gidx, l});
        }
    }

    const int K = static_cast<int>(cols.size());
    names.resize(K);
    for (int j = 0; j < K; ++j) names[j] = cols[j]._name;

    auto validCell = [&](int t, int i) {
        if (!std::isfinite(p._Y(t, i))) return false;
        for (const Mat* m : xPanels) {
            if (!std::isfinite((*m)(t, i))) return false;
        }
        for (const Vec* v : fSeries) {
            if (!std::isfinite((*v)(t))) return false;
        }
        return true;
    };

    // 注意：Eigen::MatrixXd 默认列主序，A.row(r).data() 不是连续内存，
    // 直接往里写会把行写坏。这里统一填到一个临时行向量再整体赋值。
    auto fillRow = [&](int t, int i, Vec& dst) {
        for (int j = 0; j < K; ++j) {
            const Column& c = cols[j];
            switch (c._kind) {
                case Column::Kind::Const:   dst(j) = 1.0; break;
                case Column::Kind::XPanel:  dst(j) = (*xPanels[c._idx])(t, i); break;
                case Column::Kind::FSeries: dst(j) = (*fSeries[c._idx])(t); break;
                case Column::Kind::Dummy:
                    dst(j) = (groupDefs[c._idx]._codes[i] == c._level) ? 1.0 : 0.0;
                    break;
            }
        }
    };

    // ── 2. 全样本秩判定（决定各日共同的列集合） ──
    std::vector<int> gt, gi;
    for (int t = 0; t < T; ++t) {
        for (int i = 0; i < N; ++i) {
            if (validCell(t, i)) {
                gt.push_back(t);
                gi.push_back(i);
            }
        }
    }

    std::vector<char> globalDrop(K, 0);
    Mat globalKept;
    if (!gt.empty()) {
        Mat Ag(static_cast<int>(gt.size()), K);
        Vec row(K);
        for (size_t r = 0; r < gt.size(); ++r) {
            fillRow(gt[r], gi[r], row);
            Ag.row(static_cast<int>(r)) = row.transpose();
        }

        std::vector<int> gk, gd;
        greedyRankSelect(Ag, gk, gd);
        for (int j : gd) {
            globalDrop[j] = 1;
            out._warnings.push_back("全样本剔除共线列: " + names[j]);
        }
        globalKept.resize(Ag.rows(), static_cast<int>(gk.size()));
        for (int m = 0; m < static_cast<int>(gk.size()); ++m) globalKept.col(m) = Ag.col(gk[m]);
    } else {
        out._warnings.push_back("全样本无有效观测");
        for (int j = 0; j < K; ++j) globalDrop[j] = 1;
    }

    // ── 3. 逐日回归 ──
    out._beta = Mat::Constant(T, K, kNaN);
    out._resid = Mat::Constant(T, N, kNaN);
    out._r2 = Vec::Constant(T, kNaN);
    out._n_names.assign(T, 0);
    out._dropped.assign(static_cast<size_t>(T) * K, 0);

    for (int t = 0; t < T; ++t) {
        std::vector<int> idx;
        idx.reserve(N);
        for (int i = 0; i < N; ++i) {
            if (validCell(t, i)) idx.push_back(i);
        }

        if (static_cast<int>(idx.size()) < opt._min_obs) {
            out._warnings.push_back("日期 " + p._dates[t] + " 有效标的不足，已跳过");
            continue;
        }

        Mat A(static_cast<int>(idx.size()), K);
        Vec b(static_cast<int>(idx.size()));
        Vec row(K);
        for (size_t r = 0; r < idx.size(); ++r) {
            fillRow(t, idx[r], row);
            A.row(static_cast<int>(r)) = row.transpose();
            b(static_cast<int>(r)) = p._Y(t, idx[r]);
        }
        if (opt._winsor_q > 0.0) winsorizeCrossSection(b, opt._winsor_q);

        SolveResult s = solveDesign(A, b, opt._ridge_lambda, &globalDrop);

        out._n_names[t] = static_cast<int>(idx.size());
        for (int m = 0; m < s.rank; ++m) {
            out._beta(t, s.kept[m]) = s.beta(s.kept[m]);
        }
        for (int j : s.dropped) {
            out._dropped[static_cast<size_t>(t) * K + j] = 1;
        }
        for (size_t r = 0; r < idx.size(); ++r) {
            out._resid(t, idx[r]) = s.resid(static_cast<int>(r));
        }

        const double sse = s.resid.squaredNorm();
        const double my = b.mean();
        const double sst = (b.array() - my).square().sum();
        out._r2(t) = sst > 1e-15 ? 1.0 - sse / sst : kNaN;
    }

    // ── 4. 汇总项（系数为逐日均值 + 均值检验） ──
    out._terms.resize(K);
    for (int j = 0; j < K; ++j) {
        Term& term = out._terms[j];
        term._name = names[j];
        if (globalDrop[j]) {
            term._dropped = true;
            term._drop_reason = "collinear_global";
            term._beta = kNaN;
            term._se = kNaN;
            term._t = kNaN;
            term._p = kNaN;
            continue;
        }
        std::vector<double> series(T, kNaN);
        for (int t = 0; t < T; ++t) series[t] = out._beta(t, j);
        const MeanTest mt = seriesMeanTest(series, se, opt._hac_lag);
        term._beta = mt._mean;
        term._se = mt._se;
        term._t = mt._t;
        term._p = mt._p;
    }

    out._cond = computeCond(globalKept);
    out._vif.assign(K, 0.0);
    if (opt._report_vif) {
        const std::vector<double> vifKept = computeVIF(globalKept);
        int m = 0;
        for (int j = 0; j < K; ++j) {
            if (!globalDrop[j]) {
                out._vif[j] = vifKept[m];
                ++m;
            }
        }
    }
    return out;
}

// ════════════════════════════════════════════════════════════════════════════
// 时序回归
// ════════════════════════════════════════════════════════════════════════════

TimeSeriesResult timeSeries(const PanelData& p,
                            const std::vector<std::string>& xnames,
                            const std::vector<std::string>& fnames,
                            const FitOptions& opt, SeType se) {
    TimeSeriesResult out;
    const int T = static_cast<int>(p._dates.size());
    const int N = static_cast<int>(p._symbols.size());
    out._symbols = p._symbols;

    if (T == 0 || N == 0 || p._Y.rows() != T || p._Y.cols() != N) {
        out._warnings.push_back("面板维度不一致");
        return out;
    }
    // se 降级提示：逐标的回归没有聚类 id，cluster 退化为 iid。
    if (se == SeType::Cluster) {
        out._warnings.push_back("time_series 模式不支持 cluster 标准误，已退化为 iid");
    }

    std::vector<const Mat*> xPanels;
    std::vector<const Vec*> fSeries;
    std::vector<std::string> names;

    if (opt._add_const) names.push_back("const");
    for (const auto& xn : xnames) {
        auto it = p._X.find(xn);
        if (it == p._X.end() || it->second.rows() != T || it->second.cols() != N) {
            out._warnings.push_back("截面因子缺失或维度不符: " + xn);
            continue;
        }
        names.push_back(xn);
        xPanels.push_back(&it->second);
    }
    for (const auto& fn : fnames) {
        auto it = p._F.find(fn);
        if (it == p._F.end() || it->second.size() != T) {
            out._warnings.push_back("时序因子缺失或维度不符: " + fn);
            continue;
        }
        names.push_back(fn);
        fSeries.push_back(&it->second);
    }

    const int K = static_cast<int>(names.size());
    if (K == 0) {
        out._warnings.push_back("没有可用回归项");
        return out;
    }

    out._beta = Mat::Constant(N, K, kNaN);
    out._se = Mat::Constant(N, K, kNaN);
    out._tstat = Mat::Constant(N, K, kNaN);
    out._r2 = Vec::Constant(N, kNaN);
    out._n_obs.assign(N, 0);
    out._dropped.assign(static_cast<size_t>(N) * K, 0);

    std::vector<std::vector<double>> betaSeries(K);
    for (int i = 0; i < N; ++i) {
        std::vector<int> rows;
        rows.reserve(T);
        for (int t = 0; t < T; ++t) {
            if (!std::isfinite(p._Y(t, i))) continue;
            bool ok = true;
            for (const Mat* m : xPanels) {
                if (!std::isfinite((*m)(t, i))) { ok = false; break; }
            }
            if (ok) {
                for (const Vec* v : fSeries) {
                    if (!std::isfinite((*v)(t))) { ok = false; break; }
                }
            }
            if (ok) rows.push_back(t);
        }
        out._n_obs[i] = static_cast<int>(rows.size());
        if (static_cast<int>(rows.size()) < opt._min_obs) {
            out._warnings.push_back("标的 " + p._symbols[i] + " 有效样本不足，已跳过");
            continue;
        }

        Mat A(static_cast<int>(rows.size()), K);
        Vec b(static_cast<int>(rows.size()));
        // 列顺序与 names 一致：const, xPanels（本标的自身截面因子）, fSeries
        for (size_t r = 0; r < rows.size(); ++r) {
            const int t = rows[r];
            b(static_cast<int>(r)) = p._Y(t, i);
            int col = 0;
            if (opt._add_const) {
                A(static_cast<int>(r), col++) = 1.0;
            }
            for (const Mat* m : xPanels) {
                A(static_cast<int>(r), col++) = (*m)(t, i);
            }
            for (const Vec* v : fSeries) {
                A(static_cast<int>(r), col++) = (*v)(t);
            }
        }

        SolveResult s = solveDesign(A, b, opt._ridge_lambda);
        const double sse = s.resid.squaredNorm();
        const int df = static_cast<int>(rows.size()) - s.rank;
        const double sigma2 = df > 0 ? sse / df : kNaN;
        const double my = b.mean();
        const double sst = (b.array() - my).square().sum();
        out._r2(i) = sst > 1e-15 ? 1.0 - sse / sst : kNaN;

        Mat V;
        if (se == SeType::HAC) {
            V = covHAC(s.Akept, s.resid, s.XtXinv, opt._hac_lag);
        } else {
            V = s.XtXinv * sigma2;
        }

        for (int m = 0; m < s.rank; ++m) {
            const int j = s.kept[m];
            out._beta(i, j) = s.beta(j);
            out._se(i, j) = std::sqrt(std::max(0.0, V(m, m)));
            out._tstat(i, j) = out._se(i, j) > 0.0 ? out._beta(i, j) / out._se(i, j) : kNaN;
            if (std::isfinite(out._beta(i, j))) betaSeries[j].push_back(out._beta(i, j));
        }
        for (int j : s.dropped) out._dropped[static_cast<size_t>(i) * K + j] = 1;
    }

    out._terms.resize(K);
    out._vif.assign(K, 0.0);
    for (int j = 0; j < K; ++j) {
        out._terms[j]._name = names[j];
        const MeanTest mt = seriesMeanTest(betaSeries[j], se, opt._hac_lag);
        out._terms[j]._beta = mt._mean;
        out._terms[j]._se = mt._se;
        out._terms[j]._t = mt._t;
        out._terms[j]._p = mt._p;
    }

    // 全样本设计矩阵的 VIF / 条件数
    std::vector<int> gt, gi;
    for (int t = 0; t < T; ++t) {
        for (int i = 0; i < N; ++i) {
            if (!std::isfinite(p._Y(t, i))) continue;
            bool ok = true;
            for (const Mat* m : xPanels) {
                if (!std::isfinite((*m)(t, i))) { ok = false; break; }
            }
            if (ok) {
                for (const Vec* v : fSeries) {
                    if (!std::isfinite((*v)(t))) { ok = false; break; }
                }
            }
            if (ok) { gt.push_back(t); gi.push_back(i); }
        }
    }
    if (!gt.empty()) {
        Mat Ag(static_cast<int>(gt.size()), K);
        for (size_t r = 0; r < gt.size(); ++r) {
            int col = 0;
            if (opt._add_const) Ag(static_cast<int>(r), col++) = 1.0;
            for (const Mat* m : xPanels) Ag(static_cast<int>(r), col++) = (*m)(gt[r], gi[r]);
            for (const Vec* v : fSeries) Ag(static_cast<int>(r), col++) = (*v)(gt[r]);
        }
        out._cond = computeCond(Ag);
        if (opt._report_vif) out._vif = computeVIF(Ag);
    }
    return out;
}

// ════════════════════════════════════════════════════════════════════════════
// 面板回归（双向固定效应）
// ════════════════════════════════════════════════════════════════════════════

PanelResult panel(const PanelData& p,
                  const std::vector<std::string>& xnames,
                  const FitOptions& opt, SeType se) {
    PanelResult out;
    const int T = static_cast<int>(p._dates.size());
    const int N = static_cast<int>(p._symbols.size());
    if (T == 0 || N == 0 || p._Y.rows() != T || p._Y.cols() != N) {
        out._warnings.push_back("面板维度不一致");
        return out;
    }
    // se 降级提示：面板回归只实现了按日期聚类，hac 退化为 iid。
    if (se == SeType::HAC) {
        out._warnings.push_back("panel 模式不支持 hac 标准误，已退化为 iid");
    }

    std::vector<const Mat*> xPanels;
    std::vector<std::string> names;
    for (const auto& xn : xnames) {
        auto it = p._X.find(xn);
        if (it == p._X.end() || it->second.rows() != T || it->second.cols() != N) {
            out._warnings.push_back("截面因子缺失或维度不符: " + xn);
            continue;
        }
        names.push_back(xn);
        xPanels.push_back(&it->second);
    }
    const int K = static_cast<int>(names.size());
    if (K == 0) {
        out._warnings.push_back("没有可用回归项");
        return out;
    }

    // 收集有效 (t,i)
    std::vector<int> rt, ri;
    for (int t = 0; t < T; ++t) {
        for (int i = 0; i < N; ++i) {
            if (!std::isfinite(p._Y(t, i))) continue;
            bool ok = true;
            for (const Mat* m : xPanels) {
                if (!std::isfinite((*m)(t, i))) { ok = false; break; }
            }
            if (ok) { rt.push_back(t); ri.push_back(i); }
        }
    }
    const int n = static_cast<int>(rt.size());
    out._n_obs = n;
    if (n < opt._min_obs) {
        out._warnings.push_back("有效样本不足");
        return out;
    }

    Vec y(n);
    Mat X(n, K);
    std::vector<int> dateCode(n), symbolCode(n);
    for (int r = 0; r < n; ++r) {
        y(r) = p._Y(rt[r], ri[r]);
        for (int j = 0; j < K; ++j) X(r, j) = (*xPanels[j])(rt[r], ri[r]);
        dateCode[r] = rt[r];
        symbolCode[r] = ri[r];
    }

    alternateDemean(y, X, {dateCode, symbolCode}, {T, N});

    SolveResult s = solveDesign(X, y, opt._ridge_lambda);
    const double sse = s.resid.squaredNorm();
    const double sst = (y.array() - y.mean()).square().sum();
    out._r2 = sst > 1e-15 ? 1.0 - sse / sst : kNaN;
    const int df = n - s.rank;
    out._adj_r2 = (df > 0 && std::isfinite(out._r2))
                      ? 1.0 - (1.0 - out._r2) * (n - 1.0) / df
                      : kNaN;

    double sigma2 = df > 0 ? sse / df : kNaN;
    Mat V;
    if (se == SeType::Cluster && static_cast<int>(dateCode.size()) == n) {
        V = covCluster(s.Akept, s.resid, s.XtXinv, dateCode, s.rank);
    } else {
        V = s.XtXinv * sigma2;
    }

    out._terms.resize(K);
    for (int j = 0; j < K; ++j) {
        out._terms[j]._name = names[j];
        out._terms[j]._dropped = true;
        out._terms[j]._drop_reason = "collinear";
        out._terms[j]._beta = kNaN;
        out._terms[j]._se = kNaN;
        out._terms[j]._t = kNaN;
        out._terms[j]._p = kNaN;
    }
    for (int m = 0; m < s.rank; ++m) {
        const int j = s.kept[m];
        Term& t = out._terms[j];
        t._dropped = false;
        t._drop_reason.clear();
        t._beta = s.beta(j);
        t._se = std::sqrt(std::max(0.0, V(m, m)));
        t._t = t._se > 0.0 ? t._beta / t._se : kNaN;
        t._p = studentTTwoSidedP(std::abs(t._t), df);
    }
    return out;
}

// ════════════════════════════════════════════════════════════════════════════
// FWL 部分残差化
// ════════════════════════════════════════════════════════════════════════════

void alternateDemean(Vec& y, Mat& X,
                     const std::vector<std::vector<int>>& codes,
                     const std::vector<int>& nGroups) {
    if (codes.empty()) return;

    auto demeanVec = [](Vec& v, const std::vector<int>& code, int ng) {
        Vec sum = Vec::Zero(ng);
        std::vector<int> cnt(ng, 0);
        for (int i = 0; i < v.size() && i < static_cast<int>(code.size()); ++i) {
            const int g = code[i];
            if (g < 0 || g >= ng) continue;
            sum(g) += v(i);
            ++cnt[g];
        }
        for (int i = 0; i < v.size() && i < static_cast<int>(code.size()); ++i) {
            const int g = code[i];
            if (g < 0 || g >= ng || cnt[g] == 0) continue;
            v(i) -= sum(g) / static_cast<double>(cnt[g]);
        }
    };

    // 单分组变量：一步即精确
    if (codes.size() == 1) {
        demeanVec(y, codes[0], nGroups[0]);
        for (int c = 0; c < X.cols(); ++c) {
            Vec col = X.col(c);
            demeanVec(col, codes[0], nGroups[0]);
            X.col(c) = col;
        }
        return;
    }

    // 多分组变量：交替投影到不动点
    for (int iter = 0; iter < 200; ++iter) {
        const Vec yPrev = y;
        const Mat xPrev = X;

        for (size_t g = 0; g < codes.size(); ++g) {
            demeanVec(y, codes[g], nGroups[g]);
            for (int c = 0; c < X.cols(); ++c) {
                Vec col = X.col(c);
                demeanVec(col, codes[g], nGroups[g]);
                X.col(c) = col;
            }
        }

        double delta = (y - yPrev).cwiseAbs().maxCoeff();
        if (X.cols() > 0) delta = std::max(delta, (X - xPrev).cwiseAbs().maxCoeff());
        if (delta < 1e-13) break;
    }
}

Vec partialResidual(const Vec& y, const Mat& X,
                    const std::vector<std::vector<int>>& codes,
                    const std::vector<int>& nGroups, bool addConst) {
    Vec y1 = y;
    Mat X1 = X;
    alternateDemean(y1, X1, codes, nGroups);

    if (X1.cols() == 0) return y1;

    const int n = static_cast<int>(X1.rows());
    const int off = addConst ? 1 : 0;
    Mat A(n, X1.cols() + off);
    if (addConst) A.col(0).setOnes();
    for (int j = 0; j < X1.cols(); ++j) A.col(j + off) = X1.col(j);

    SolveResult s = solveDesign(A, y1, 0.0);
    return s.resid;
}

}  // namespace regression
