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

// Python 子进程 stdout/stderr 的 readLine 空闲超时（毫秒）。
// 训练/优化脚本只在每个 trial/阶段边界输出一行，而单个 trial 的
// train+backtest 可超过 1 分钟；超时过短会把仍在工作的子进程误判为
// "脚本未输出 result" 并对其发 SIGTERM。
constexpr int QS_PYTHON_READLINE_TIMEOUT_MS = 300000;

// 取出 Python 输出行 JSON 里的 "type" 字段；不是 JSON 对象或没有 type 时返回空串。
//
// 不能用子串匹配 find("\"type\":\"error\"")：Python 端 json.dumps 不带
// separators 时输出的是 {"type": "error", ...}（冒号后有空格），子串匹配会漏判，
// 于是真实错误既没进 stderrLines，也没进 resultLine，最终只报出
// "optimize script 未输出 result" 这种无信息量的兜底文案。
inline String jsonLineType(const String& line) {
    try {
        auto j = nlohmann::json::parse(line);
        if (j.is_object() && j.contains("type") && j["type"].is_string())
            return j["type"].get<String>();
    } catch (...) {}
    return String();
}

// 从 Python 的错误行里取出可读文案：优先 message 字段，其次 error 字段，
// 都不是就退回原始行。避免把整段 JSON 甩给调用方。
inline String jsonLineMessage(const String& line) {
    try {
        auto j = nlohmann::json::parse(line);
        if (j.is_object()) {
            if (j.contains("message") && j["message"].is_string()) return j["message"].get<String>();
            if (j.contains("error") && j["error"].is_string()) return j["error"].get<String>();
        }
    } catch (...) {}
    return line;
}

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
    // 推送 DONE 哨兵：幂等，保证消费者（readLine）一定被唤醒且只会收到一次 DONE
    void drainAndFinish(int exit_code);

    static void splitLines(std::string& buf, PythonOutput::Type type,
                           std::queue<PythonOutput>& q, std::mutex& mtx,
                           std::condition_variable& cv);

    std::atomic<bool> running_{false};
    // DONE 哨兵是否已推送
    std::atomic<bool> _donePushed{false};
    // 子进程是否已被 waitpid 回收：防止重复回收与 PID 复用后误杀无关进程
    std::atomic<bool> _childReaped{false};
    // 子进程退出码
    std::atomic<int> _exitCode{0};
    // reader 线程：可 join，保证析构时它不会再访问 this
    std::thread reader_;

    std::queue<PythonOutput> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;

#ifdef _WIN32
    HANDLE hProcess_  = nullptr;
    HANDLE hThread_   = nullptr;
    HANDLE hStdoutRd_ = nullptr;
    HANDLE hStderrRd_ = nullptr;
#else
    std::atomic<pid_t> pid_{-1};
    int stdout_fd_  = -1;
    int stderr_fd_  = -1;

    // 终止并回收子进程：并发调用时由 waitpid 天然互斥，只有一个回收者能成功
    int terminateAndReap(pid_t pid);
    // 把 waitpid 的 status 翻译成退出码（被信号杀死时返回 -signal）
    static int decodeExitStatus(int status, pid_t pid);
#endif
};
