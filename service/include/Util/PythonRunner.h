#pragma once
#include "std_header.h"
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>
#include "json.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#endif

struct PythonOutput {
    enum Type { STDOUT, STDERR, DONE };
    Type type;
    std::string line;
    int exit_code = 0;
};

// Python 环境配置
// env 参数支持三种形式：
//   1. 空/不传       → 使用 config.json 中 python.default
//   2. 配置中的环境名 → 查 python.environments 映射
//   3. 绝对路径       → 直接作为解释器路径
struct PythonEnv {
    std::string default_interpreter = "python3";                    // 默认解释器
    Map<String, String> environments;                                // 环境名 → 解释器路径

    // 从 config.json 的 "python" 段加载
    static PythonEnv fromConfig(const nlohmann::json& cfg);

    // 解析 env 参数为实际解释器路径
    std::string resolve(const std::string& env = "") const;
};

class PythonRunner {
public:
    PythonRunner() = default;
    ~PythonRunner();

    // 启动脚本（非阻塞）
    // interpreter: python 解释器路径（由 PythonEnv::resolve 返回）
    bool start(const std::string& script,
               const std::vector<std::string>& args = {},
               const std::string& interpreter = "python3");

    // 读取下一行输出（阻塞，直到有输出或超时）
    bool readLine(PythonOutput& out, int timeout_ms = 5000);

    void kill();
    bool isRunning() const;

private:
    void readerThread();
    void drainAndFinish(int exit_code);

    static void splitLines(std::string& buf, PythonOutput::Type type,
                           std::queue<PythonOutput>& q, std::mutex& mtx,
                           std::condition_variable& cv);

    std::atomic<bool> running_{false};

    std::queue<PythonOutput> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;

#ifdef _WIN32
    HANDLE hProcess_  = nullptr;
    HANDLE hThread_   = nullptr;
    HANDLE hStdoutRd_ = nullptr;
    HANDLE hStderrRd_ = nullptr;
#else
    pid_t pid_      = -1;
    int stdout_fd_  = -1;
    int stderr_fd_  = -1;
#endif
};
