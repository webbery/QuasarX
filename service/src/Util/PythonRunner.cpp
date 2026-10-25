#include "Util/PythonRunner.h"
#include "Util/string_algorithm.h"
#include "Util/datetime.h"
#include "Util/log.h"

// ═══════════════════════════════════════════════════════════
//  PythonEnv — 环境解析
// ═══════════════════════════════════════════════════════════

PythonEnv PythonEnv::fromConfig(const nlohmann::json& cfg) {
    PythonEnv env;
    if (!cfg.contains("python")) return env;

    auto& py = cfg["python"];
    if (py.contains("default"))
        env.default_interpreter = py["default"].get<std::string>();

    if (py.contains("environments") && py["environments"].is_object()) {
        for (auto& [name, path] : py["environments"].items()) {
            env.environments[name] = path.get<std::string>();
        }
    }
    return env;
}

std::string PythonEnv::resolve(const std::string& env) const {
    if (env.empty())
        return default_interpreter;

    // 绝对路径 → 直接使用
    if (env.front() == '/' || (env.size() > 1 && env[1] == ':'))
        return env;

    // 查环境映射
    auto it = environments.find(env);
    if (it != environments.end())
        return it->second;

    // 未找到，回退默认
    WARN("PythonEnv: unknown env '{}', fallback to '{}'", env, default_interpreter);
    return default_interpreter;
}

// ═══════════════════════════════════════════════════════════
//  跨平台共用
// ═══════════════════════════════════════════════════════════

void PythonRunner::splitLines(std::string& buf, PythonOutput::Type type,
                               std::queue<PythonOutput>& q, std::mutex& mtx,
                               std::condition_variable& cv) {
    size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        {
            std::lock_guard<std::mutex> lock(mtx);
            q.push({type, std::move(line)});
        }
        cv.notify_one();
        buf.erase(0, pos + 1);
    }
}

void PythonRunner::drainAndFinish(int exit_code) {
    // 幂等：DONE 哨兵只推送一次。子进程被 kill 时由 kill() 推送，
    // 正常结束时由 reader 线程推送，谁先到谁算，避免重复 DONE 打断消费者。
    bool expected = false;
    if (!_donePushed.compare_exchange_strong(expected, true)) return;

    _exitCode.store(exit_code);
    {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push({PythonOutput::DONE, "", exit_code});
    }
    cv_.notify_one();
    running_ = false;
    INFO("PythonRunner: finished with exit_code={}", exit_code);
}

bool PythonRunner::readLine(PythonOutput& out, int timeout_ms) {
    std::unique_lock<std::mutex> lock(mtx_);
    if (cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [this]{ return !queue_.empty(); })) {
        out = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    return false;
}

bool PythonRunner::isRunning() const {
    return running_;
}

#ifdef _WIN32
// ═══════════════════════════════════════════════════════════
//  Windows 实现
// ═══════════════════════════════════════════════════════════

static bool createInheritPipe(HANDLE& hRead, HANDLE& hWrite) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = nullptr;
    sa.bInheritHandle = TRUE;
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return false;
    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);
    return true;
}

bool PythonRunner::start(const std::string& script,
                         const std::vector<std::string>& args,
                         const std::string& interpreter) {
    HANDLE hOutRd, hOutWr, hErrRd, hErrWr;
    if (!createInheritPipe(hOutRd, hOutWr) || !createInheritPipe(hErrRd, hErrWr))
        return false;

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutWr;
    si.hStdError  = hErrWr;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);

    // 构建命令行: interpreter -u script arg1 arg2 ...
    // -u: 强制 stdout/stderr 无缓冲，防止管道模式下进程退出时输出丢失
    std::string cmdline = interpreter + " -u " + script;
    for (auto& a : args) {
        // 空字符串或含引号/花括号/空格的参数需要用引号包裹并转义内部双引号（如 JSON）
        bool needQuote = a.empty() || a.find_first_of(" \"{}[]") != std::string::npos;
        if (needQuote) {
            std::string escaped = "\"";
            for (char c : a) {
                if (c == '"') escaped += "\\\"";
                else escaped += c;
            }
            escaped += "\"";
            cmdline += " " + escaped;
        } else {
            cmdline += " " + a;
        }
    }

    BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr,
                             TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hOutWr);
    CloseHandle(hErrWr);

    if (!ok) {
        CloseHandle(hOutRd);
        CloseHandle(hErrRd);
        WARN("PythonRunner: CreateProcess failed: {} {}", interpreter, script);
        return false;
    }

    hProcess_  = pi.hProcess;
    hThread_   = pi.hThread;
    hStdoutRd_ = hOutRd;
    hStderrRd_ = hErrRd;
    running_   = true;

    // 保留线程句柄：析构时 join，确保 reader 线程不会再访问已释放的 this
    reader_ = std::thread(&PythonRunner::readerThread, this);
    INFO("PythonRunner: started pid={} {} {}", pi.dwProcessId, interpreter, script);
    return true;
}

void PythonRunner::readerThread() {
    char buf[4096];
    std::string stdout_buf, stderr_buf;
    HANDLE handles[2] = {hStdoutRd_, hStderrRd_};
    // 进程句柄快照：kill() 会关闭并置空成员变量，这里用快照避免读到空/已关闭句柄
    HANDLE hProc = hProcess_;

    while (running_) {
        bool any_data = false;
        for (int i = 0; i < 2; i++) {
            DWORD avail = 0;
            if (!PeekNamedPipe(handles[i], nullptr, 0, nullptr, &avail, nullptr))
                continue;
            if (avail == 0) continue;

            any_data = true;
            DWORD bytesRead = 0;
            DWORD toRead = (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf);
            if (!ReadFile(handles[i], buf, toRead, &bytesRead, nullptr))
                continue;

            auto type = (i == 0) ? PythonOutput::STDOUT : PythonOutput::STDERR;
            auto& sbuf = (i == 0) ? stdout_buf : stderr_buf;
            sbuf.append(buf, bytesRead);
            splitLines(sbuf, type, queue_, mtx_, cv_);
        }

        if (!any_data) {
            DWORD wait = WaitForSingleObject(hProc, 100);
            if (wait == WAIT_OBJECT_0) {
                for (int i = 0; i < 2; i++) {
                    while (true) {
                        DWORD avail = 0;
                        if (!PeekNamedPipe(handles[i], nullptr, 0, nullptr, &avail, nullptr) || avail == 0)
                            break;
                        DWORD bytesRead = 0;
                        ReadFile(handles[i], buf, (avail < sizeof(buf)) ? avail : (DWORD)sizeof(buf), &bytesRead, nullptr);
                        auto type = (i == 0) ? PythonOutput::STDOUT : PythonOutput::STDERR;
                        auto& sbuf = (i == 0) ? stdout_buf : stderr_buf;
                        sbuf.append(buf, bytesRead);
                        splitLines(sbuf, type, queue_, mtx_, cv_);
                    }
                }
                // 推送 drain 后残留的无换行尾数据（防止进程退出时最后一行丢失）
                auto flushRemnant = [&](std::string& remnant, PythonOutput::Type type) {
                    if (!remnant.empty()) {
                        std::lock_guard<std::mutex> lock(mtx_);
                        queue_.push({type, std::move(remnant)});
                        cv_.notify_one();
                    }
                };
                flushRemnant(stdout_buf, PythonOutput::STDOUT);
                flushRemnant(stderr_buf, PythonOutput::STDERR);

                DWORD exit_code = 0;
                GetExitCodeProcess(hProc, &exit_code);
                drainAndFinish((int)exit_code);
                break;
            }
        }
    }

    // 退出路径（被 kill 打断时也会走到这里）：推送 DONE 哨兵，
    // 保证消费者不会一直阻塞在 readLine 上等下一次输出。
    drainAndFinish(_exitCode.load());

    CloseHandle(hStdoutRd_);
    CloseHandle(hStderrRd_);
    hStdoutRd_ = nullptr;
    hStderrRd_ = nullptr;
}

void PythonRunner::kill() {
    if (hProcess_) {
        TerminateProcess(hProcess_, 1);
        WaitForSingleObject(hProcess_, 3000);
        DWORD exit_code = 0;
        GetExitCodeProcess(hProcess_, &exit_code);
        _exitCode.store((int)exit_code);
        CloseHandle(hProcess_);
        CloseHandle(hThread_);
        hProcess_ = nullptr;
        hThread_  = nullptr;
    }
    running_ = false;
    // 消费者（readLine）必须被唤醒：子进程没了就不会再有输出，
    // 这里补推 DONE 哨兵，避免消费者空等到超时。
    drainAndFinish(_exitCode.load());
}

PythonRunner::~PythonRunner() {
    kill();
    // join reader 线程：它退出后不再访问 this，避免对象释放后的 use-after-free
    if (reader_.joinable()) reader_.join();
}

#else
// ═══════════════════════════════════════════════════════════
//  Linux 实现
// ═══════════════════════════════════════════════════════════

#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>
#include <cerrno>
#include <chrono>
#include <cstring>

bool PythonRunner::start(const std::string& script,
                         const std::vector<std::string>& args,
                         const std::string& interpreter) {
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0) {
        WARN("PythonRunner: pipe(out_pipe) failed: {}", strerror(errno));
        return false;
    }
    if (pipe(err_pipe) != 0) {
        WARN("PythonRunner: pipe(err_pipe) failed: {}", strerror(errno));
        close(out_pipe[0]); close(out_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        WARN("PythonRunner: fork() failed: {}", strerror(errno));
        close(out_pipe[0]); close(out_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        return false;
    }

    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        std::vector<const char*> argv;
        argv.push_back(interpreter.c_str());
        argv.push_back("-u");  // 强制无缓冲，防止管道模式下输出丢失
        argv.push_back(script.c_str());
        for (auto& a : args) argv.push_back(a.c_str());
        argv.push_back(nullptr);

        execvp(interpreter.c_str(), const_cast<char**>(argv.data()));
        _exit(127);
    }

    close(out_pipe[1]);
    close(err_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);

    pid_       = pid;
    stdout_fd_ = out_pipe[0];
    stderr_fd_ = err_pipe[0];
    running_   = true;

    // 保留线程句柄：析构时 join，确保 reader 线程不会再访问已释放的 this
    reader_ = std::thread(&PythonRunner::readerThread, this);
    INFO("PythonRunner: started pid={} {} {}", pid, interpreter, script);
    return true;
}

void PythonRunner::readerThread() {
    char buf[4096];
    std::string stdout_buf, stderr_buf;

    // pid 本地快照：kill() 会把成员 pid_ 置为 -1，若继续用成员变量 waitpid 会退化成
    // waitpid(-1)，变成回收任意子进程（可能误伤其它 PythonRunner），必须用快照
    const pid_t my_pid = pid_.load();

    struct pollfd fds[2] = {
        {stdout_fd_, POLLIN, 0},
        {stderr_fd_, POLLIN, 0}
    };

    while (running_) {
        int ret = poll(fds, 2, 200);
        if (ret < 0) break;

        for (int i = 0; i < 2; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP))) continue;
            int fd = fds[i].fd;
            auto type = (fd == stdout_fd_) ? PythonOutput::STDOUT : PythonOutput::STDERR;
            auto& sbuf = (fd == stdout_fd_) ? stdout_buf : stderr_buf;

            ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0) continue;
            sbuf.append(buf, n);
            splitLines(sbuf, type, queue_, mtx_, cv_);
        }

        if (my_pid > 0) {
            int status;
            pid_t ret_pid = waitpid(my_pid, &status, WNOHANG);
            if (ret_pid == my_pid) {
                _childReaped.store(true);
                for (int i = 0; i < 2; i++) {
                    while (true) {
                        ssize_t n = ::read(fds[i].fd, buf, sizeof(buf));
                        if (n <= 0) break;
                        auto type = (fds[i].fd == stdout_fd_) ? PythonOutput::STDOUT : PythonOutput::STDERR;
                        auto& sbuf = (fds[i].fd == stdout_fd_) ? stdout_buf : stderr_buf;
                        sbuf.append(buf, n);
                        splitLines(sbuf, type, queue_, mtx_, cv_);
                    }
                }
                // 推送 drain 后残留的无换行尾数据
                auto flushRemnant = [&](std::string& remnant, PythonOutput::Type type) {
                    if (!remnant.empty()) {
                        std::lock_guard<std::mutex> lock(mtx_);
                        queue_.push({type, std::move(remnant)});
                        cv_.notify_one();
                    }
                };
                flushRemnant(stdout_buf, PythonOutput::STDOUT);
                flushRemnant(stderr_buf, PythonOutput::STDERR);

                drainAndFinish(decodeExitStatus(status));
                break;
            }
            // ECHILD：子进程已被 kill() 那侧回收，本线程无需再 waitpid，退出等待收尾
            if (ret_pid < 0 && errno == ECHILD) break;
        }
    }

    // 统一收尾：
    // 1) 子进程若还没人回收，这里负责回收，避免留下僵尸进程
    // 2) 推送 DONE 哨兵，保证消费者不会空等到 readLine 超时
    if (my_pid > 0 && !_childReaped.load()) {
        terminateAndReap(my_pid);
    }
    drainAndFinish(_exitCode.load());

    close(stdout_fd_);
    close(stderr_fd_);
    stdout_fd_ = -1;
    stderr_fd_ = -1;
}

int PythonRunner::decodeExitStatus(int status) {
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        WARN("PythonRunner: child pid={} killed by signal {} ({})", pid_.load(), sig, strsignal(sig));
        return -sig;
    }
    return -1;
}

int PythonRunner::terminateAndReap(pid_t pid) {
    if (pid <= 0) return _exitCode.load();

    ::kill(pid, SIGTERM);

    int status = 0;
    pid_t ret = 0;
    // 等待子进程退出；忽略 SIGTERM 的脚本最多等 ~2s，之后 SIGKILL 兜底
    for (int i = 0; i < 40; i++) {
        ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) break;
        if (ret < 0) {
            // ECHILD：已被另一线程回收（waitpid 对同一 pid 天然互斥）
            _childReaped.store(true);
            return _exitCode.load();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (ret == 0) {
        ::kill(pid, SIGKILL);
        do {
            ret = waitpid(pid, &status, 0);
        } while (ret < 0 && errno == EINTR);
    }
    if (ret == pid) {
        _childReaped.store(true);
        int code = decodeExitStatus(status);
        _exitCode.store(code);
        return code;
    }
    return _exitCode.load();
}

void PythonRunner::kill() {
    pid_t pid = pid_.load();
    if (pid > 0 && !_childReaped.load()) {
        // 终止并回收子进程。回收完成后立刻推 DONE 哨兵，
        // 消费者无需再等下一次输出或 readLine 超时。
        terminateAndReap(pid);
    }
    pid_ = -1;
    running_ = false;
    drainAndFinish(_exitCode.load());
}

PythonRunner::~PythonRunner() {
    kill();
    // join reader 线程：它退出后不再访问 this，避免对象释放后的 use-after-free
    if (reader_.joinable()) reader_.join();
}

#endif
