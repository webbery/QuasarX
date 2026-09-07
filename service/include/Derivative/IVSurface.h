#pragma once
#include "std_header.h"
#include "Derivative/OptionPricer.h"
#include <cmath>
#include <map>

// 通用自然三次样条插值（double 类型 x，适用于 IV 曲面等金融场景）
// 与 EMD_SIMD.h 中的 natural_cubic_spline 区别：x 为 double（非 int 索引）
class CubicSpline {
public:
    CubicSpline() = default;

    // 从数据点构建样条
    // xs: 单调递增的 x 坐标, ys: 对应 y 值
    void fit(const Eigen::VectorXd& xs, const Eigen::VectorXd& ys);

    // 在指定 x 处求值
    double eval(double x) const;

    // 批量求值
    Eigen::VectorXd eval(const Eigen::VectorXd& xs) const;

    bool empty() const { return _a.size() == 0; }

private:
    // 每段系数: S_i(x) = a[i] + b[i]*(x-x[i]) + c[i]*(x-x[i])^2 + d[i]*(x-x[i])^3
    Eigen::VectorXd _x;   // 节点
    Eigen::VectorXd _a, _b, _c, _d;

    // 二分查找 x 所在区间
    int findSegment(double x) const;
};

// 从期权市场价格反算隐含波动率 (Newton-Raphson + Black-Scholes)
// price: 期权市场价格 (close), S: 标的价格, K: 行权价, T: 年化到期时间, r: 无风险利率
// 返回 0.0 表示反算失败
double computeIVFromPrice(double price, double S, double K, double T, double r, bool is_call);

// IV 曲面: (strike, expiry_days, opt_type) → implied_volatility
// 数据来源: OptionDataDB option_daily 表
// L6 修复: call 和 put 分开构造样条,不再简单平均
class IVSurface {
public:
    struct IVPoint {
        double strike;
        int expiry_days;
        double iv;
        OptionType opt_type = OptionType::Unknown;
    };

    // 从原始数据点构建曲面
    // points: 每个合约的 (strike, expiry_days, iv, opt_type)
    void build(const Vector<IVPoint>& points);

    // 插值: 给定 (strike, expiry_days, opt_type) 返回插值后的 IV
    // 先沿 strike 维度 cubic spline 插值每个 expiry → 再沿 expiry 线性插值
    // opt_type 为 Unknown 时使用合并样条(向后兼容)
    double interpolate(double strike, int expiry_days, OptionType opt_type = OptionType::Unknown) const;

    // 生成曲面网格数据（供前端 3D/2D 可视化）
    // strikes: 行权价序列, expiry_days_list: 到期天数序列
    // 返回: 两个二维数组 [call_surface, put_surface], 每个 [expiry_idx][strike_idx] = iv
    // 如果数据中无 call_put 信息,put_surface 为空
    std::pair<Vector<Vector<double>>, Vector<Vector<double>>> generateSurface(
        const Vector<double>& strikes,
        const Vector<int>& expiry_days_list) const;

    // 获取原始数据点（供散点叠加）
    const Vector<IVPoint>& rawPoints() const { return _points; }

    bool empty() const { return _points.empty(); }

private:
    Vector<IVPoint> _points;

    // 按 (expiry_days, opt_type) 分组的样条
    // key: (expiry_days, opt_type) → CubicSpline(strike → iv)
    std::map<std::pair<int, OptionType>, CubicSpline> _splines;

    // 合并样条(向后兼容,call_put 为空时使用)
    std::map<int, CubicSpline> _splines_merged;

    // 排序后的 expiry 列表（用于线性插值）
    Vector<int> _expiry_list;
};
