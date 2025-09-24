#include <memory_resource>
#ifdef WIN32
#else
#include <execinfo.h>
#include <cxxabi.h>
#include <unistd.h>
#include <getopt.h>
#include <signal.h>
#endif
#include "server.h"
#include "Util/string_algorithm.h"
#ifdef WIN32
#define popen _popen
#define pclose _pclose
#endif
#include "spdlog/sinks/rotating_file_sink.h"

#define CMD_RESULT_BUF_SIZE 2048

int exec(const std::string& cmd, std::string& result)
{
    int iRet = -1;
    char buf_ps[CMD_RESULT_BUF_SIZE] = {0};
    FILE *ptr;

    if((ptr = popen(cmd.c_str(), "r")) != NULL)
    {
        while(fgets(buf_ps, sizeof(buf_ps), ptr) != NULL)
        {
           result += std::string(buf_ps);
        }
        if (result.back() == '\r' || result.back() == '\n') {
            result.pop_back();
        }
        pclose(ptr);
        ptr = NULL;
        iRet = 0;  // 处理成功
    }
    else
    {
        iRet = -1; // 处理失败
    }

    return iRet;
}

#ifdef WIN32
LONG CALLBACK unhandled_exception_filter(EXCEPTION_POINTERS* exception_info) {
  // 获取堆栈信息
  PVOID stack[10];
  DWORD frames;
  printf("111\n");
  if (!RtlCaptureStackBackTrace(0, 100, NULL, &frames))
  {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  printf("333\n");
  if (!RtlCaptureStackBackTrace(0, 10, stack, &frames)) {
    printf("Failed to capture stack trace.\n");
  }
  else {
    for (ULONG i = 0; i < frames; i++) {
      //PVOID frame = stack[i];
      //printf("Frame %u: ", i);
      //PCHAR symbol_name = NULL;
      //ULONG symbol_size = 0;
      //if (SymFromAddr(GetCurrentProcess(), (PVOID)frame, &symbol_name, &symbol_size)) {
      //  printf("%s\n", symbol_name);
      //  LocalFree(symbol_name);
      //}
      //else {
      //  printf("Unable to get symbol name.\n");
      //}
    }
  }
  // 返回异常处理的结果
  printf("222\n");
  return EXCEPTION_EXECUTE_HANDLER;
}
#else
void print_stacktrace(int signo) {
    void *array[10];
    size_t size;

    // 获取当前线程的堆栈地址
    // 注意：这里假设只有一个线程，如果需要处理多线程情况，请使用pthread_getspecific等函数
    array[0] = (void *) pthread_self();

    // 获取堆栈地址数量
    size = backtrace(array, sizeof(array) / sizeof(array[0]));
    size_t max_output_size = (std::min)((int)size, 20);

    // 打印堆栈地址
    char **strings = backtrace_symbols(array, max_output_size);
    for (size_t i = 0; i < max_output_size; ++i) {
      std::vector<std::string> info;
      split((std::string)strings[i], info, " ");
      for (auto& str: info) {
        if (str[0] == '[' && str.back() == ']') {
          std::string addr = str.substr(1, str.size() - 2);
          std::string cmd = "addr2line " + addr + " -e " + GetProgramPath();
          std::string output;
          exec(cmd, output);
          printf("[%s] ", output.c_str());
        } else {
          printf("%s ", str.c_str());
        }
      }
      // 使用__cxa_demangle函数解析符号信息
      // char name[256] = {0};
      // int status = 0;
      // size_t len = 256;
      // __cxxabiv1::__cxa_demangle(strings[i], name, &len, 0);
      // if (status == 0 || strlen(name) != 0) {
      //     printf("%s ", name);
      //     // 注意：这里假设所有的函数都在同一个文件中定义，实际上可能并非如此
      //     // printf("Line: %d ", atoi(name) + 1);  // 注意：这里的行号是从1开始的
      // } else {
      // }
      printf("\n");
    }

    // 释放内存
    free(strings);
    exit(-1);
}
#endif

void install_signal_handler() {
#ifdef WIN32
  SetUnhandledExceptionFilter(unhandled_exception_filter);
#else
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = print_stacktrace;
  sigaction(SIGSEGV, &action, NULL);
  sigaction(SIGFPE, &action, NULL);
#endif
}

void init_logger() {
    auto console_sink = std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>();
    console_sink->set_level(spdlog::level::info); // 控制台输出级别

    // 文件sink（每周回转）
    // 日志文件路径，例如：logs/weekly_log.txt
    std::string log_file_path = "logs/monthly_log.txt";
    // 设置文件大小限制（例如100MB）和备份文件数量
    size_t max_file_size = 1024 * 1024 * 100; // 100 MB
    size_t max_files = 4; // 保留最近4周的日志备份
    auto combined_logger = spdlog::rotating_logger_mt(
        ToString(Now()).c_str(), log_file_path.c_str(), max_file_size, max_files);
    combined_logger->sinks().push_back(console_sink);
    // 4. 设置日志格式
    combined_logger->set_pattern("[%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");

    // 5. 设置全局日志级别（可选，logger会继承这个级别）
    combined_logger->set_level(spdlog::level::trace);

    
    // 7. 设置为默认logger（可选，这样可以直接使用spdlog::info()等函数）
    spdlog::set_default_logger(combined_logger);
}

int main(int argc, char* argv[])
{
    std::pmr::synchronized_pool_resource pool;
    std::pmr::set_default_resource(&pool);
    hmdf::ThreadGranularity::set_optimum_thread_level();
    
    install_signal_handler();

    // init log
    init_logger();

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
    spdlog::shutdown();
    return 0;
}