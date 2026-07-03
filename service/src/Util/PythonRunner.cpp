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

    // 构建命令行: interpreter script arg1 arg2 ...
    std::string cmdline = interpreter + " " + script;
    for (auto& a : args) cmdline += " " + a;

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

    std::thread(&PythonRunner::readerThread, this).detach();
    INFO("PythonRunner: started pid={} {} {}", pi.dwProcessId, interpreter, script);
    return true;
}

void PythonRunner::readerThread() {
    char buf[4096];
    std::string stdout_buf, stderr_buf;
    HANDLE handles[2] = {hStdoutRd_, hStderrRd_};

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
            DWORD wait = WaitForSingleObject(hProcess_, 100);
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
                DWORD exit_code = 0;
                GetExitCodeProcess(hProcess_, &exit_code);
                drainAndFinish((int)exit_code);
                break;
            }
        }
    }

    CloseHandle(hStdoutRd_);
    CloseHandle(hStderrRd_);
    hStdoutRd_ = nullptr;
    hStderrRd_ = nullptr;
}

void PythonRunner::kill() {
    if (hProcess_) {
        TerminateProcess(hProcess_, 1);
        WaitForSingleObject(hProcess_, 3000);
        CloseHandle(hProcess_);
        CloseHandle(hThread_);
        hProcess_ = nullptr;
        hThread_  = nullptr;
    }
    running_ = false;
}

PythonRunner::~PythonRunner() {
    kill();
}

#else
// ═══════════════════════════════════════════════════════════
//  Linux 实现
// ═══════════════════════════════════════════════════════════

#include <signal.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <poll.h>

bool PythonRunner::start(const std::string& script,
                         const std::vector<std::string>& args,
                         const std::string& interpreter) {
    int out_pipe[2], err_pipe[2];
    if (pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) return false;

    if (pid == 0) {
        close(out_pipe[0]);
        close(err_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(out_pipe[1]);
        close(err_pipe[1]);

        std::vector<const char*> argv;
        argv.push_back(interpreter.c_str());
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

    std::thread(&PythonRunner::readerThread, this).detach();
    INFO("PythonRunner: started pid={} {} {}", pid, interpreter, script);
    return true;
}

void PythonRunner::readerThread() {
    char buf[4096];
    std::string stdout_buf, stderr_buf;

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

        int status;
        pid_t ret_pid = waitpid(pid_, &status, WNOHANG);
        if (ret_pid > 0) {
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
            int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            drainAndFinish(exit_code);
            break;
        }
    }

    close(stdout_fd_);
    close(stderr_fd_);
    stdout_fd_ = -1;
    stderr_fd_ = -1;
}

void PythonRunner::kill() {
    if (pid_ > 0) {
        ::kill(pid_, SIGTERM);
        int status;
        waitpid(pid_, &status, 0);
        pid_ = -1;
    }
    running_ = false;
}

PythonRunner::~PythonRunner() {
    kill();
}

#endif
