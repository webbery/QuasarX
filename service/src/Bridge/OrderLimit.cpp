#include "Bridge/OrderLimit.h"

OrderLimit::OrderLimit(int rate, int capacity)
    : timePerToken_(Nanoseconds(1000000000) / rate),
      timePerBurst_((capacity - 1) * Nanoseconds(1000000000) / rate), // 计算突发容量对应的时间范围
      _capcity(capacity),
      nextFreeTime_(Clock::now()) // 初始化为当前时间，表示令牌桶初始为满
{
}

bool OrderLimit::tryConsume()
{
    return tryConsume(1);
}

bool OrderLimit::tryConsume(uint64_t tokens)
{
    const auto now = Clock::now();
    const Nanoseconds timeNeeded = tokens * timePerToken_;
    const auto minTime = now - timePerBurst_; // 计算允许消费的最早开始时间

    auto oldNextTime = nextFreeTime_.load(std::memory_order_relaxed);
    for (;;) {
        // 使用临时变量进行计算，避免直接操作原子对象
        auto newNextTime = oldNextTime;
        
        // 如果当前请求距离上次成功消费的时间间隔超过了突发容量允许的范围，
        // 则从当前时间减去突发容量对应的时间开始计算，避免因长期空闲导致的下一次突发过大
        if (minTime > newNextTime) {
            newNextTime = minTime;
        }
        newNextTime += timeNeeded;

        // 如果新的时间点在未来，说明令牌不足，消费失败
        if (newNextTime > now) {
            return false;
        }

        // 使用原子操作比较并交换，确保线程安全地更新下一次可用时间
        if (nextFreeTime_.compare_exchange_weak(
            oldNextTime, newNextTime,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
            return true;
        }
        // 如果交换失败（说明其他线程修改了nextFreeTime_），则循环重试
    }
}