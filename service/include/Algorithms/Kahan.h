#pragma once

/**
 * @brief Kahan 补偿求和累加器
 * 
 * 用于降低浮点数递推求和的累积误差。
 * 普通递推：误差 O(n*ε)，Kahan：误差 O(ε)
 * 
 * 用法：
 *   KahanAccumulator acc;
 *   acc.add(1.0);
 *   acc.add(2.0);
 *   acc.sub(0.5);
 *   double sum = acc.sum();
 */
class KahanAccumulator {
public:
    KahanAccumulator() : _sum(0.0), _compensation(0.0) {}
    
    void add(double value) {
        double y = value - _compensation;
        double t = _sum + y;
        _compensation = (t - _sum) - y;
        _sum = t;
    }
    
    void sub(double value) {
        add(-value);
    }
    
    double sum() const { return _sum; }
    
    void reset() {
        _sum = 0.0;
        _compensation = 0.0;
    }

private:
    double _sum;
    double _compensation;
};
