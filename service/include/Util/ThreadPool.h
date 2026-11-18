#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include "Util/log.h"

namespace QuasarX {

// ── 通用线程池（固定 worker 数的队列）────────────────────────
//
// 为什么要池化：批量下载任务会拆成多个 group，旧实现给每个 group 起一个
// detached 线程，线程里再为每只股票 fork 一个 python3 下载脚本，
// 峰值 50+ 个 python3 × 25~30MB ≈ 1.3GB —— 在 1.6GB 的机器上直接
// global OOM，QuantService 被 OOM killer 杀掉（status=137）。
//
// 现在用固定 worker 数跑队列：
//   - worker 数 = std::thread::hardware_concurrency()（硬件线程数）
//   - 同时存活的 python3 下载进程数因此有硬上限
//   - 夜间批量更新会变慢（排队执行），这是用内存换稳定性的自觉取舍
class ThreadPool {
public:
    static ThreadPool& instance() {
        static ThreadPool inst;
        return inst;
    }

    void submit(std::function<void()> task) {
        if (!task) return;
        {
            std::lock_guard<std::mutex> lk(_mtx);
            if (_stopping) {
                WARN("[ThreadPool] executor is shutting down, task dropped");
                return;
            }
            _queue.push_back(std::move(task));
        }
        _cv.notify_one();
    }

    /// @brief 停止接新任务并等在跑的任务收尾（退出前调用）
    void shutdown() {
        {
            std::lock_guard<std::mutex> lk(_mtx);
            if (_stopping) return;
            _stopping = true;
        }
        _cv.notify_all();
        for (auto& t : _workers) {
            if (t.joinable()) t.join();
        }
        _workers.clear();
    }

    size_t workerCount() const { return _workers.size(); }
    size_t pendingCount() const {
        std::lock_guard<std::mutex> lk(_mtx);
        return _queue.size();
    }

private:
    ThreadPool() {
        unsigned int hc = std::thread::hardware_concurrency();
        size_t n = (hc > 0) ? static_cast<size_t>(hc) : 1;
        _workers.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            _workers.emplace_back([this] { workerLoop(); });
        }
        INFO("[ThreadPool] thread pool started: {} worker(s)", n);
    }

    ~ThreadPool() {
        shutdown();
    }

    void workerLoop() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(_mtx);
                _cv.wait(lk, [this] { return _stopping || !_queue.empty(); });
                if (_queue.empty()) {
                    if (_stopping) return;   // 停机且队列已空 → 退出
                    continue;
                }
                task = std::move(_queue.front());
                _queue.pop_front();
            }
            try {
                task();
            } catch (const std::exception& e) {
                WARN("[ThreadPool] task failed: {}", e.what());
            } catch (...) {
                WARN("[ThreadPool] task failed with unknown exception");
            }
        }
    }

    mutable std::mutex _mtx;
    std::condition_variable _cv;
    std::deque<std::function<void()>> _queue;
    std::vector<std::thread> _workers;
    bool _stopping = false;
};

}  // namespace QuasarX
