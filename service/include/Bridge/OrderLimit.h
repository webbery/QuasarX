#pragma once
#include <chrono>
#include <atomic>

using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;

/*
 * 订单流量监控
*/
class OrderLimit {
public:
    // @params maxTimes 每秒最大次数
    OrderLimit(int rate, int capcity);

    // 尝试消费一个令牌
    // 返回true表示成功（未超过限制），false表示失败（已超过限制）
    bool tryConsume();
    // 尝试消费指定数量的令牌
    bool tryConsume(uint64_t tokens);
private:
    const Nanoseconds timePerToken_; // 生成一个令牌需要的时间
    const Nanoseconds timePerBurst_; // 允许的最大突发量对应的时间窗口
    std::atomic<Clock::time_point> nextFreeTime_; // 下一次可以无等待消费令牌的时间点
};