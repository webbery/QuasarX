#include "spdlog/common.h"
#include <memory_resource>
#include <exception>  // std::set_terminate
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN  // 阻止 windows.h 包含 winsock.h（避免 sockaddr 重定义）
#include <windows.h>
#include <dbghelp.h>
#include <excpt.h>
#pragma comment(lib, "dbghelp.lib")
#else
#include <execinfo.h>
#include <cxxabi.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <sys/wait.h>  // waitpid
#include <vector>
#include <string>
#endif
#include "server.h"
#include "Util/string_algorithm.h"
#include "Util/DuckDBLogger.h"
#ifdef WIN32
#define popen _popen
#define pclose _pclose
#endif
#include "spdlog/sinks/rotating_file_sink.h"
#include "Util/system.h"

#define CMD_RESULT_BUF_SIZE 2048

#ifdef WIN32
LONG CALLBACK unhandled_exception_filter(EXCEPTION_POINTERS* exception_info) {
    fprintf(stderr, "\n========== Windows Unhandled Exception ==========\n");
    fprintf(stderr, "Exception code: 0x%08X\n", exception_info->ExceptionRecord->ExceptionCode);
    fprintf(stderr, "Exception address: 0x%p\n", exception_info->ExceptionRecord->ExceptionAddress);
    
    // 初始化 DbgHelp（只需一次）
    static bool initialized = false;
    if (!initialized) {
        SymInitialize(GetCurrentProcess(), NULL, TRUE);
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
        initialized = true;
    }
    
    // 捕获堆栈
    PVOID stack[64];
    USHORT frames = CaptureStackBackTrace(0, 64, stack, NULL);
    
    fprintf(stderr, "\nStack trace (%u frames):\n", frames);
    
    // 解析每个帧的符号
    for (USHORT i = 0; i < frames; i++) {
        DWORD64 address = (DWORD64)stack[i];
        DWORD64 dwDisplacement = 0;
        
        // 分配足够的内存用于 SYMBOL_INFO 结构
        char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
        PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;
        
        // 获取符号信息
        if (SymFromAddr(GetCurrentProcess(), address, &dwDisplacement, pSymbol)) {
            // 获取行号信息
            IMAGEHLP_LINE64 lineInfo = {0};
            lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
            DWORD lineDisplacement = 0;
            
            if (SymGetLineFromAddr64(GetCurrentProcess(), address, &lineDisplacement, &lineInfo)) {
                fprintf(stderr, "  [%02u] %s + 0x%I64x (%s:%lu)\n",
                        i, pSymbol->Name, dwDisplacement, lineInfo.FileName, lineInfo.LineNumber);
            } else {
                fprintf(stderr, "  [%02u] %s + 0x%I64x\n",
                        i, pSymbol->Name, dwDisplacement);
            }
        } else {
            fprintf(stderr, "  [%02u] 0x%p (no symbol)\n", i, stack[i]);
        }
    }
    
    fprintf(stderr, "\n=========================================\n");
    fprintf(stderr, "Check crash dump or attach debugger for more details.\n\n");
    
    return EXCEPTION_EXECUTE_HANDLER;
}
#else
// 异步信号安全的堆栈打印（信号处理函数中只能调用 async-signal-safe 函数）
void print_stacktrace(int signo) {
    // 使用 write() 直接输出到 stderr，这是 async-signal-safe 的
    const char* signal_name = strsignal(signo);
    const char prefix[] = "\n=== CRASH: Caught signal ";
    write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    
    char signum_buf[16];
    int signum_len = 0;
    int tmp = signo;
    do {
        signum_buf[signum_len++] = '0' + (tmp % 10);
        tmp /= 10;
    } while (tmp > 0);
    for (int i = 0; i < signum_len / 2; i++) {
        char c = signum_buf[i];
        signum_buf[i] = signum_buf[signum_len - 1 - i];
        signum_buf[signum_len - 1 - i] = c;
    }
    signum_buf[signum_len] = '\0';
    write(STDERR_FILENO, signum_buf, signum_len);
    write(STDERR_FILENO, " (", 2);
    write(STDERR_FILENO, signal_name, strlen(signal_name));
    write(STDERR_FILENO, ") ===\n", 6);

    // 捕获堆栈（增加到 64 帧）
    void* array[64];
    int size = backtrace(array, 64);
    
    // 获取符号信息
    char** strings = backtrace_symbols(array, size);
    if (strings == NULL) {
        const char err_msg[] = "Failed to get backtrace_symbols\n";
        write(STDERR_FILENO, err_msg, sizeof(err_msg) - 1);
        _exit(EXIT_FAILURE);
    }

    // 打印原始堆栈信息（async-signal-safe）
    const char stack_header[] = "\nStack trace:\n";
    write(STDERR_FILENO, stack_header, sizeof(stack_header) - 1);
    
    for (int i = 0; i < size; i++) {
        char line[512];
        int len = snprintf(line, sizeof(line), "  [%02d] %s\n", i, strings[i]);
        write(STDERR_FILENO, line, len);
    }

    // 尝试 demangle C++ 符号（也在信号处理函数中安全执行）
    const char demangle_header[] = "\nDemangled stack trace:\n";
    write(STDERR_FILENO, demangle_header, sizeof(demangle_header) - 1);
    
    for (int i = 0; i < size; i++) {
        // 解析 backtrace_symbols 输出格式: ./binary(mangled_name+0xoffset) [0xaddr]
        char* begin = strchr(strings[i], '(');
        char* end = begin ? strchr(begin, '+') : NULL;
        
        if (begin && end) {
            *begin = '\0';
            *end = '\0';
            begin++;
            
            int status = 0;
            char* demangled = abi::__cxa_demangle(begin, NULL, NULL, &status);
            
            char line[512];
            int len;
            if (status == 0 && demangled) {
                len = snprintf(line, sizeof(line), "  [%02d] %s(%s+0x", i, strings[i], demangled);
                free(demangled);
            } else {
                len = snprintf(line, sizeof(line), "  [%02d] %s(%s+0x", i, strings[i], begin);
            }
            
            // 恢复原字符串以便打印 offset
            *end = '+';
            char* offset_start = end + 1;
            char* bracket = strchr(offset_start, ')');
            if (bracket) *bracket = '\0';
            len += snprintf(line + len, sizeof(line) - len, "%s) [", offset_start);
            if (bracket) *bracket = ')';
            
            // 打印地址
            char* addr_begin = strchr(strings[i], '[');
            if (addr_begin) {
                len += snprintf(line + len, sizeof(line) - len, "%s", addr_begin);
            }
            len += snprintf(line + len, sizeof(line) - len, "\n");
            write(STDERR_FILENO, line, len);
        } else {
            // 无法解析，直接打印原始字符串
            char line[512];
            int len = snprintf(line, sizeof(line), "  [%02d] %s\n", i, strings[i]);
            write(STDERR_FILENO, line, len);
        }
    }

    // 提示用户使用 addr2line 获取详细信息
    const char tip[] = "\nTo get source locations, run:\n  addr2line -e <binary> -a <address>\n";
    write(STDERR_FILENO, tip, sizeof(tip) - 1);
    
    const char example[] = "Example: addr2line -e ./QuantService -a 0x401234\n\n";
    write(STDERR_FILENO, example, sizeof(example) - 1);

    free(strings);
    _exit(EXIT_FAILURE);  // 使用 _exit 而不是 exit，避免调用 atexit 处理函数
}
#endif

void install_signal_handler() {
#ifdef WIN32
  SetUnhandledExceptionFilter(unhandled_exception_filter);
#else
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = print_stacktrace;
  sigemptyset(&action.sa_mask);
  
  // 注册所有关键崩溃信号
  sigaction(SIGSEGV, &action, NULL);   // 段错误（非法内存访问）
  sigaction(SIGFPE, &action, NULL);    // 浮点异常（除零等）
  sigaction(SIGABRT, &action, NULL);   // abort() 调用（assert 失败、异常终止）
  sigaction(SIGILL, &action, NULL);    // 非法指令
  sigaction(SIGBUS, &action, NULL);    // 总线错误
  sigaction(SIGTERM, &action, NULL);   // 终止信号
#endif
}

// C++ terminate handler（捕获 std::terminate，通常是未捕获异常）
void on_terminate() {
#ifdef WIN32
    // Windows: 使用 fprintf 输出到 stderr
    fprintf(stderr, "\n========== std::terminate called ==========\n");
    fprintf(stderr, "Attempting to print stack trace...\n\n");
    
    // 尝试获取当前异常信息
    try {
        throw;  // 重新抛出当前异常
    } catch (const std::exception& e) {
        fprintf(stderr, "Active exception: %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "Unknown exception type\n");
    }
    
    // Windows 堆栈打印（需要 DbgHelp，留给 unhandled_exception_filter 处理）
    fprintf(stderr, "\nCheck crash dump for full analysis.\n");
    fprintf(stderr, "=========================================\n\n");
#else
    // Linux: 使用 write() 直接输出（async-signal-safe）
    const char msg[] = "\n========== std::terminate called ==========\n"
                       "Attempting to print stack trace...\n\n";
    write(STDERR_FILENO, msg, sizeof(msg) - 1);
    
    // 尝试获取当前异常信息
    try {
        throw;  // 重新抛出当前异常
    } catch (const std::exception& e) {
        const char exc_msg[] = "Active exception: ";
        write(STDERR_FILENO, exc_msg, sizeof(exc_msg) - 1);
        write(STDERR_FILENO, e.what(), strlen(e.what()));
        write(STDERR_FILENO, "\n", 1);
    } catch (...) {
        const char unknown_exc[] = "Unknown exception type\n";
        write(STDERR_FILENO, unknown_exc, sizeof(unknown_exc) - 1);
    }
    
    // 打印堆栈
    void* array[64];
    int size = backtrace(array, 64);
    char** strings = backtrace_symbols(array, size);
    
    if (strings) {
        for (int i = 0; i < size; i++) {
            char line[512];
            int len = snprintf(line, sizeof(line), "  [%02d] %s\n", i, strings[i]);
            write(STDERR_FILENO, line, len);
        }
        
        // 解析地址为源文件行号（fork 子进程调用 addr2line）
        const char resolve_header[] = "\n--- Source location resolution ---\n";
        write(STDERR_FILENO, resolve_header, sizeof(resolve_header) - 1);
        
        // 收集所有地址
        std::vector<std::string> addresses;
        for (int i = 0; i < size; i++) {
            // 解析格式: ./binary(symbol+0xoffset) [0xADDR]
            char* addr_start = strchr(strings[i], '[');
            if (addr_start) {
                char* addr_end = strchr(addr_start, ']');
                if (addr_end) {
                    *addr_end = '\0';
                    addresses.push_back(addr_start + 1);  // 跳过 '['
                    *addr_end = ']';
                }
            }
        }
        
        if (!addresses.empty()) {
            // 获取当前可执行文件路径
            char exe_path[4096];
            ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
            if (len == -1) {
                const char err_msg[] = "Failed to get executable path\n";
                write(STDERR_FILENO, err_msg, sizeof(err_msg) - 1);
            } else {
                exe_path[len] = '\0';
                
                // 优先使用 .debug 文件（如果存在）
                std::string debug_path = std::string(exe_path) + ".debug";
                const char* target = access(debug_path.c_str(), F_OK) == 0 ? debug_path.c_str() : exe_path;
                
                // fork 子进程执行 addr2line
                pid_t pid = fork();
                if (pid == 0) {
                    // 子进程：执行 addr2line
                    std::vector<const char*> args;
                    args.push_back("addr2line");
                    args.push_back("-e");
                    args.push_back(target);
                    args.push_back("-f");  // 打印函数名
                    args.push_back("-C");  // demangle C++ 符号
                    for (const auto& addr : addresses) {
                        args.push_back(addr.c_str());
                    }
                    args.push_back(nullptr);
                    
                    execvp("addr2line", const_cast<char**>(args.data()));
                    // 如果 execvp 失败
                    _exit(127);
                } else if (pid > 0) {
                    // 父进程：等待子进程完成
                    int status;
                    waitpid(pid, &status, 0);
                } else {
                    const char fork_err[] = "fork() failed, skipping addr2line\n";
                    write(STDERR_FILENO, fork_err, sizeof(fork_err) - 1);
                }
            }
        }
        
        free(strings);
    }
    
    const char footer[] = "\nCheck core dump for full GDB analysis.\n"
                          "=========================================\n\n";
    write(STDERR_FILENO, footer, sizeof(footer) - 1);
#endif
    
    // 调用默认 terminate handler（会 abort() 触发 SIGABRT）
    std::abort();
}

void init_logger() {
    // 文件sink（每周回转）
    // 日志文件路径，例如：logs/weekly_log.txt
    std::string log_file_path = "logs/monthly_log.txt";
    // 设置文件大小限制（例如100MB）和备份文件数量
    size_t max_file_size = 1024 * 1024 * 100; // 100 MB
    size_t max_files = 4; // 保留最近4周的日志备份
    auto combined_logger = spdlog::rotating_logger_mt(
        ToString(Now()).c_str(), log_file_path.c_str(), max_file_size, max_files);
    // 4. 设置日志格式
    combined_logger->set_pattern("[%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] [%s:%#] %v");

    // 5. 设置全局日志级别（可选，logger会继承这个级别）
#ifdef _DEBUG
    combined_logger->set_level(spdlog::level::debug);
#else
    combined_logger->set_level(spdlog::level::trace);
#endif
    // 警告则刷新
    combined_logger->flush_on(spdlog::level::warn);
    // 每60秒刷新日志
    spdlog::flush_every(std::chrono::seconds(60));

    
    // 7. 设置为默认logger（可选，这样可以直接使用spdlog::info()等函数）
    spdlog::set_default_logger(combined_logger);
}

int main(int argc, char* argv[])
{
    printf("VERSION: %s\n", QS_VERSION);
    std::pmr::synchronized_pool_resource pool;
    std::pmr::set_default_resource(&pool);
    hmdf::ThreadGranularity::set_optimum_thread_level();

    // 安装信号处理器和 terminate handler
    install_signal_handler();
    std::set_terminate(on_terminate);

    // init log
    init_logger();

    // 初始化 DuckDB 策略日志（表不存在则自动创建）
    if (!DuckDBLogger::instance().init("logs/strategy_logs.db")) {
        SPDLOG_WARN("[Main] DuckDB logger init failed, strategy logs will not be stored");
    }

    Server server;
    // 加载配置
    if (argc == 2) {
        // 服务模式: xxx.config
        if (!server.Init(argv[1])) {
          return -1;
        }
    } else {
        if (!server.Init("./config.json")) {
          return -1;
        }
    }
    
    server.Run();

    // 关闭 DuckDB 日志器
    DuckDBLogger::instance().shutdown();
    spdlog::shutdown();

    return 0;
}