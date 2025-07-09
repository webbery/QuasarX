#include "Algorithms/Random.h"
#include <random>

Vector<double> gauss_noise(double mean, double sigma, int count) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> dist(mean, sigma); // 均值 0，标准差 1

    Vector<double> result(count);
    for (int i = 0; i < count; ++i) {
        result[i] = dist(gen); // 生成一个随机数
    }
    return result;
}

Vector<double> gauss_noise_simd(double mean, double sigma, int count) {
    return gauss_noise(mean, sigma, count);
}