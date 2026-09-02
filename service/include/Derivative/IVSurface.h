#pragma once
#include "std_header.h"
#include <cmath>
#include <map>

// 通用自然三次样条插值（double 类型 x，适用于 IV 曲面等金融场景）
// 与 EMD_SIMD.h 中的 natural_cubic_spline 区别：x 为 double（非 int 索引）
class CubicSpline {
public:
    CubicSpline() = default;

    // 从数据点构建样条
    // xs: 单调递增的 x 坐标, ys: 对应 y 值
    void fit(const Vector<double>& xs, const Vector<double>& ys);

    // 在指定 x 处求值
    double eval(double x) const;

    // 批量求值
    Vector<double> eval(const Vector<double>& xs) const;

    bool empty() const { return _a.empty(); }

private:
    // 每段系数: S_i(x) = a[i] + b[i]*(x-x[i]) + c[i]*(x-x[i])^2 + d[i]*(x-x[i])^3
    Vector<double> _x;   // 节点
    Vector<double> _a, _b, _c, _d;

    // 二分查找 x 所在区间
    int findSegment(double x) const;
};

// IV 曲面: (strike, expiry_days) → implied_volatility
// 数据来源: OptionDataDB option_daily 表
class IVSurface {
public:
    struct IVPoint {
        double strike;
        int expiry_days;
        double iv;
    };

    // 从原始数据点构建曲面
    // points: 每个合约的 (strike, expiry_days, iv)
    void build(const Vector<IVPoint>& points);

    // 插值: 给定 (strike, expiry_days) 返回插值后的 IV
    // 先沿 strike 维度 cubic spline 插值每个 expiry → 再沿 expiry 线性插值
    double interpolate(double strike, int expiry_days) const;

    // 生成曲面网格数据（供前端 3D/2D 可视化）
    // strikes: 行权价序列, expiry_days_list: 到期天数序列
    // 返回: 二维数组 [expiry_idx][strike_idx] = iv
    Vector<Vector<double>> generateSurface(
        const Vector<double>& strikes,
        const Vector<int>& expiry_days_list) const;

    // 获取原始数据点（供散点叠加）
    const Vector<IVPoint>& rawPoints() const { return _points; }

    bool empty() const { return _points.empty(); }

private:
    Vector<IVPoint> _points;

    // 按 expiry_days 分组的样条: expiry_days → CubicSpline(strike → iv)
    std::map<int, CubicSpline> _splines_by_expiry;

    // 排序后的 expiry 列表（用于线性插值）
    Vector<int> _expiry_list;
};
