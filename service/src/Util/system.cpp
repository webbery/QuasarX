#include "Util/system.h"
#include "Bridge/CTP/CTPSymbol.h"
#include "Util/string_algorithm.h"
#include "nng/protocol/pubsub0/sub.h"
#include "nng/protocol/pubsub0/pub.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <functional>
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib") // 链接Winsock库
#else
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <pthread.h>
#include <sys/wait.h>
#endif
#include <vector>
#include <fstream>
#include "server.h"
#include "DataSource.h"

#ifdef HAVE_CUDA
#include <cuda_runtime.h>
#endif
#include <stdio.h>
#ifdef WIN32
#define popen _popen
#define pclose _pclose
#endif

std::string GetIP() {
#ifdef WIN32
  WSADATA wsaData;
  char hostname[256];
  struct addrinfo hints, *res, *ptr;
  int status, addrCount = 0;

  // 初始化Winsock
  status = WSAStartup(MAKEWORD(2, 2), &wsaData);
  if (status != 0) {
    return "";
  }

  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC; // 不指定IP版本，IPv4或IPv6
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  // 获取本地主机名
  gethostname(hostname, sizeof(hostname));

  // 解析主机名以获取地址信息
  status = getaddrinfo(hostname, NULL, &hints, &res);
  if (status != 0) {
    WSACleanup();
    return "";
  }

  std::vector<std::string> ip4;
  // 遍历地址信息链表
  for (ptr = res; ptr != NULL; ptr = ptr->ai_next) {
    void* addrPtr = nullptr;
    if (ptr->ai_family == AF_INET) {
      struct sockaddr_in* ipv4 = reinterpret_cast<struct sockaddr_in*>(ptr->ai_addr);
      addrPtr = &(ipv4->sin_addr);
    }
    else {
      continue;
      //struct sockaddr_in6* ipv6 = reinterpret_cast<struct sockaddr_in6*>(ptr->ai_addr);
      //addrPtr = &(ipv6->sin6_addr);
    }

    // 打印IP地址
    char ipstr[INET6_ADDRSTRLEN];
    inet_ntop(ptr->ai_family, addrPtr, ipstr, sizeof(ipstr));

    std::vector<std::string> tokens;
    split(ipstr, tokens, ".");
    if (tokens.size() == 4 && tokens[3] == "1")
      continue;
    ip4.push_back(ipstr);
  }

  freeaddrinfo(res); // 释放地址信息链表
  WSACleanup(); // 清理Winsock

  if (ip4.size() == 1)
    return ip4.front();
  return "";
#else
  struct ifaddrs *ifaddr, *ifa;
    int family, s;
    
    // 获取网络接口地址
    if (getifaddrs(&ifaddr) == -1) {
        return "";
    }

    std::vector<std::string> ip4;
    // 遍历所有接口
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        // 只处理IPv4地址
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        // 获取IP地址
        char host[INET_ADDRSTRLEN];
        if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in), host, sizeof(host), nullptr, 0, NI_NUMERICHOST) == 0) {
            // std::cout << ifa->ifa_name << ": " << host << std::endl;
            std::vector<std::string> tokens;
            split((String)host, tokens, ".");
            if (tokens.size() == 4 && tokens[2] == "0" && tokens[3] == "1")
                continue;
                
            ip4.push_back(host);
        }
    }

    // 释放接口地址
    freeifaddrs(ifaddr);
    if (ip4.size() == 1)
        return ip4.front();
    return "";
#endif
}

bool RunCommand(const std::string& cmd) {
  if (system(cmd.c_str())) {
    FATAL("run command {} fail.", cmd);
    return false;
  }
  return true;
}

bool RunCommand(const std::string& cmd, String& output) {
#ifdef WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = NULL;
    sa.bInheritHandle = TRUE;

    HANDLE hStdoutRd, hStdoutWr;
    if (!CreatePipe(&hStdoutRd, &hStdoutWr, &sa, 0)) {
        return false;
    }
    SetHandleInformation(hStdoutRd, HANDLE_FLAG_INHERIT, 0);

    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdoutWr;
    si.hStdError = hStdoutWr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);

    std::string command = "cmd /c " + cmd;  // 使用cmd执行命令

    BOOL success = CreateProcessA(
        NULL,
        command.data(),
        NULL,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &si,
        &pi
    );

    if (!success) {
        CloseHandle(hStdoutWr);
        CloseHandle(hStdoutRd);
        return false;
    }

    CloseHandle(hStdoutWr);

    const int BUFFER_SIZE = 4096;
    char buffer[BUFFER_SIZE];
    DWORD bytesRead;
    output.clear();

    while (true) {
        BOOL readSuccess = ReadFile(hStdoutRd, buffer, BUFFER_SIZE - 1, &bytesRead, NULL);
        if (!readSuccess || bytesRead == 0) {
            break;
    }
        buffer[bytesRead] = '\0';
        output += buffer;
}

    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hStdoutRd);
#else
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return false;
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) { // 子进程
        close(pipefd[0]); // 关闭读端
        dup2(pipefd[1], STDOUT_FILENO); // 将标准输出重定向到管道
        close(pipefd[1]);

        Vector<String> args;
        split(cmd, args, " ");
        // 准备参数数组供 execvp 使用
        std::vector<char*> argv;
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr); // 参数数组必须以 nullptr 结尾

        // 执行程序（第一个参数为程序路径）
        execvp(argv[0], argv.data());
        exit(1); // 如果 execvp 失败
    } else { // 父进程
        close(pipefd[1]); // 关闭写端

        std::array<char, 128> buffer;
        ssize_t bytes_read;
        while ((bytes_read = read(pipefd[0], buffer.data(), buffer.size() - 1)) > 0) {
            output.append(buffer.data(), bytes_read);
        }

        close(pipefd[0]);
        waitpid(pid, nullptr, 0); // 等待子进程结束
        return true;
    }
#endif
  return true;
}

int MovetoLineStart(std::ifstream* fs) {
    fs->seekg(-1, std::ios_base::cur);
    for (int i = fs->tellg(); i > 0; i--) {
        if (fs->peek() == '\n') {
            fs->get();
            return i;
        }
        fs->seekg(i, std::ios_base::beg);
    }
    return -1;
}

void GetLinesReverse(std::ifstream* fs) {
    // Go to the last character before EOF
    fs->seekg(-1, std::ios_base::end);
    int pos = MovetoLineStart(fs);
    int count = 10;
    std::string lastline = "";
    while (pos > 0) {
        if (count <= 0) {
            return;
        }
        getline(*fs, lastline);
        fs->seekg(pos);
        pos = MovetoLineStart(fs);
        --count;
    }
}

std::string GetLastLine(std::ifstream* fs) {
    // Go to the last character before EOF
    fs->seekg(-1, std::ios_base::end);
    if (MovetoLineStart(fs) < 0) {
        fs->close();
        return "";
    }
    std::string lastline = "";
    std::getline(*fs, lastline);
    fs->close();
    return lastline;
}

std::vector<StockRowInfo> ReadCSV(const std::string& csv, int last_N) {
  std::ifstream file(csv);
    
  if (!file.is_open()) {
      throw std::runtime_error("Unable to open file");
  }

  // 找到文件末尾
  file.seekg(0, std::ios::end);
  std::streampos end = file.tellg();

  std::vector<StockRowInfo> rows(last_N);
  // 从末尾开始读取指定行数
  file.seekg(-1, std::ios_base::end);
  int pos = MovetoLineStart(&file);
  std::string line;
  while (pos > 0) {
      if (last_N <= 0) {
          break;
      }
      getline(file, line);
      if (line.empty()) {
        break;
      }

      std::vector<std::string> tokens;
      split(line, tokens, ",");
      auto& v0 = tokens[0];
      auto v1 = atof(tokens[1].c_str());
      auto v2 = atof(tokens[2].c_str());
      auto v3 = atof(tokens[3].c_str());
      auto v4 = atof(tokens[4].c_str());
      auto v5 = atof(tokens[5].c_str());
      auto v6 = atof(tokens[6].c_str());
      auto v7 = atof(tokens[7].c_str());
      auto tp = std::make_tuple(v0, v1, v2, v3, v4, v5, v6, v7);
      rows[last_N - 1] = tp;
      
      file.seekg(pos);
      pos = MovetoLineStart(&file);
      --last_N;
  }
  if (last_N > 0) {
    return {rows.begin() + last_N, rows.end()};
  }
  return rows;
}

bool Subscribe(const std::string& uri, nng_socket& sock, short tick) {
  int rv = nng_sub0_open(&sock);
  if (rv != 0) {
    printf("ERROR: nng_sub0_open fail.\n");
    return false;
  }

  if ((rv = nng_socket_set(sock, NNG_OPT_SUB_SUBSCRIBE, "", 0)) != 0) {
    printf("ERROR: nng_socket_set_string fail: %s\n", nng_strerror(rv));
    return false;
  }
  if ((rv = nng_socket_set_ms(sock, NNG_OPT_RECVTIMEO, tick)) != 0) {
    printf("ERROR: nng_socket_set_int fail: %s\n", nng_strerror(rv));
    return false;
  }
  
  rv = nng_dial(sock, uri.c_str(), nullptr, NNG_FLAG_NONBLOCK);//NNG_FLAG_NONBLOCK
  if (rv != 0) {
    printf("ERROR: nng_dial fail: %s.\n", nng_strerror(rv));
    return false;
  }
  return true;
}

bool Publish(const std::string& uri, nng_socket& sock)
{
  int rv = nng_pub0_open(&sock);
  if (rv != 0) {
    printf("ERROR: nng_pub0_open fail.\n");
    return false;
  }
  rv = nng_listen(sock, uri.c_str(), nullptr, 0);
  if (rv < 0) {
    printf("ERROR: nng_listen fail: %s.\n", nng_strerror(rv));
    return false;
  }
  return true;
}

bool ReadQuote(nng_socket& sock, QuoteInfo& quote, const Set<symbol_t>& filter) {
  char* buff = NULL;
  size_t sz = 0;
  int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
  if (rv != 0) {
      nng_free(buff, sz);
      return false;
  }
  constexpr static std::size_t flags = yas::mem | yas::binary;
  yas::shared_buffer buf;
  buf.assign(buff, sz);
  yas::load<flags>(buf, quote);
  nng_free(buff, sz);
  if (!filter.empty()) {
    if (filter.count(quote._symbol) == 0)
      return false;;
  }
  return true;
}

bool ReadFeatures(nng_socket& sock, DataFeatures& features) {
  char* buff = NULL;
  size_t sz = 0;
  int rv = nng_recv(sock, &buff, &sz, NNG_FLAG_ALLOC);
  if (rv != 0) {
      nng_free(buff, sz);
      return false;
  }
  constexpr static std::size_t flags = yas::mem | yas::binary;
  yas::shared_buffer buf;
  buf.assign(buff, sz);
  yas::load<flags>(buf, features);
  nng_free(buff, sz);

  return true;
}

#ifdef _WIN32
void __cpuid(unsigned int output[4], unsigned int EAX, unsigned int ECX) {
}
unsigned int __xgetbv(unsigned int ECX) {
    return 0;
}
#else
void __cpuid (unsigned int output[4], unsigned int EAX, unsigned int ECX) {    
  unsigned int a, b, c, d;
  __asm("cpuid"  : "=a"(a),"=b"(b),"=c"(c),"=d"(d) : "a"(EAX),"c"(ECX) : );
  output[0] = a;
  output[1] = b;
  output[2] = c;
  output[3] = d;
}

unsigned int __xgetbv (unsigned int ECX) {
  unsigned int ret = 0;
  __asm("xgetbv" : "=a"(ret) : "c"(ECX) : );
  return ret;
}
#endif

bool IsSupportCuda() {
#ifdef HAS_CUDA
#endif
  return false;
}

SIMDSupport GetSIMDSupport() {
    unsigned int CPUInfo[4] = {0}, ECX = 0, EDX = 0, XCR0_EAX = 0;
    __cpuid(CPUInfo, 1, 0);
    ECX = CPUInfo[2];
    EDX = CPUInfo[3];
 
    if(EDX & 0x00800000)
        return SIMDSupport::MMX;
    if(EDX & 0x02000000)
        return SIMDSupport::SSE;
    if(EDX & 0x04000000)
        printf("CPU Support SSE2\n");
    if(ECX & 1)
        printf("CPU Support SSE3\n");
    if(ECX & 0x00000200)
        printf("CPU Support SSSE3\n");
    if(ECX & 0x00080000)
        printf("CPU Support SSE4.1\n");
    if(ECX & 0x00100000)
        printf("CPU Support SSE4.2\n");
    if(ECX & 0x02000000)
        printf("CPU Support AES\n");
    if(ECX & 0x10000000)
        printf("CPU Support AVX\n");
    if(ECX & 0x08000000)
        printf("OS Support XSAVE\n");
    else{
        printf("OS not Support XSAVE, OS not Support SIMD\n");
        return SIMDSupport::None;
    }
 
    XCR0_EAX = __xgetbv(0);
    if(XCR0_EAX & 0x00000002)
        printf("OS Support SSE/SSE2/SSE3/SSE4\n");
    if(XCR0_EAX & 0x00000004)
        printf("OS Support AVX\n");
    if(XCR0_EAX & 0x00000040)
        printf("OS Support AVX-512\n");
    return SIMDSupport::None;
}

void SetCurrentThreadName(const char* name) {
    // Linux/macOS (使用 pthread)
#if defined(__APPLE__) || defined(__linux__)
    pthread_t this_thread = pthread_self();
    pthread_setname_np(this_thread, name);
#elif defined(_WIN32)
    // Windows 10+ 支持
    wchar_t wname[256];
    mbstowcs(wname, name, 256);
    SetThreadDescription(GetCurrentThread(), wname);
#endif
    // 其他平台可能无实现
}

namespace {
  const Map<String, char> exchange_map{{"SZ", MT_Shenzhen}, {"SH", MT_Shanghai}, {"BJ", MT_Beijing}};
}

symbol_t to_symbol(const String& symbol, const String& exchange) {
  List<String> tokens;
  split(symbol, tokens, ".");
  String strSymbol = tokens.back();
  symbol_t id;
  memset(&id, 0, sizeof(symbol_t));
  auto code = atoi(symbol.c_str());
  if (exchange.empty()) {
    auto ct = Server::GetContractType(strSymbol);
    switch (ct) {
    case ContractType::AStock: id._type = contract_type::stock; break;
    case ContractType::ETF: id._type = contract_type::fund; break;
    case ContractType::Future: id._type = contract_type::future; break;
    case ContractType::Option: {
      id._type = contract_type::call;
    }
    case ContractType::Index: id._type = contract_type::index; break;
    break;
    default: break;
    }
    if (tokens.size() > 1) {
      auto excName = to_upper(tokens.front());
      id._exchange = exchange_map.at(excName);
    } else {
      id._exchange = Server::GetExchange(strSymbol);
    }
  } else {
    id._type = contract_type::stock;
    id._exchange = exchange_map.at(exchange);
  }
  id._symbol = code;
  return id;
}

String get_symbol(const symbol_t& symbol) {
  if (symbol._type == contract_type::future) {
    return CTPObjectName(symbol._opt) + std::to_string(symbol._symbol);
  }
  else if (symbol._type == contract_type::put || symbol._type == contract_type::call) {
    char buff[5] = {0};
    snprintf(buff, 5, "%02d%02d", symbol._year, symbol._month);
    char CP = (symbol._type == contract_type::put? 'P':'C');
    return CTPObjectName(symbol._opt) + buff + CP + std::to_string(symbol._price * 100);
  }
  else if (symbol._type != contract_type::put || symbol._type != contract_type::call) {
#define CHINA_STOCK_SIZE 7
    String head;
    if (symbol._exchange == MT_Shenzhen) {
      head = "sz.";
    }
    else if (symbol._exchange == MT_Shanghai) {
      head = "sh.";
    }
    else if (symbol._exchange == MT_Beijing) {
      head = "bj.";
    }
    if (!head.empty()) {
      char buff[CHINA_STOCK_SIZE] = {0};
      snprintf(buff, CHINA_STOCK_SIZE, "%06d", symbol._symbol);
      return head + String(buff, 6);
    }
    
    return format_symbol(std::to_string(symbol._symbol));
  }
  WARN("unknow symbol type: {}", (int)symbol._type);
  return std::to_string(*(int32_t*)&symbol);
}

bool operator==(const symbol_t& left, const symbol_t& right) {
  return memcmp(&left, &right, sizeof(symbol_t)) == 0;
}

bool operator<(const symbol_t& left, const symbol_t& right) {
  return memcmp(&left, &right, sizeof(symbol_t)) < 0;
}

bool is_future(symbol_t sym) {
  return sym._type == contract_type::future;
}

bool is_stock(symbol_t sym) {
  return sym._type == contract_type::stock;
}
bool is_option(symbol_t sym) {
  return sym._type == contract_type::call || sym._type == contract_type::put;
}
bool is_fund(symbol_t sym) {
  return sym._type == contract_type::fund;
}

Set<symbol_t> get_holds(const AccountPosition& account) {
  Set<symbol_t> holds;
  for (auto& item: account._positions) {
    holds.insert(item._symbol);
  }
  return holds;
}

std::string GetProgramPath() {
#ifdef WIN32
    TCHAR path[MAX_PATH] = { 0 };
    if (GetModuleFileName(NULL, path, MAX_PATH) == 0) {
        return "";
    }
    return path;
#else
    const std::size_t MAXBUFSIZE = 2048;
    char buf[MAXBUFSIZE] = { '\0' };
    auto size = readlink("/proc/self/exe", buf, MAXBUFSIZE);
    return std::string(buf, size);
#endif
}

#ifdef __linux__
CPUPerformanceMesure::CPUPerformanceMesure() {
    int retval = PAPI_library_init(PAPI_VER_CURRENT);
    if (retval != PAPI_VER_CURRENT) {
        fprintf(stderr, "PAPI init error: %s\n", PAPI_strerror(retval));
        return ;
    }
    if (PAPI_create_eventset(&EventSet) != PAPI_OK) {
        fprintf(stderr, "Failed to create event set\n");
        return ;
    }
}

bool CPUPerformanceMesure::RegistEvents(const Vector<int>& events) {
  // 添加总指令数和时钟周期
  // int events[] = { PAPI_TOT_INS, PAPI_TOT_CYC };
  if (PAPI_add_events(EventSet, (int*)&(events[0]), events.size()) != PAPI_OK) {
      fprintf(stderr, "Failed to add events\n");
      return false;
  }
  return true;
}

bool CPUPerformanceMesure::Start() {
  return PAPI_start(EventSet) == PAPI_OK;
}

bool CPUPerformanceMesure::Stop(Vector<long long>& values) {
  if (PAPI_stop(EventSet, &values[0]) != PAPI_OK) {
      fprintf(stderr, "Failed to stop counting\n");
      return false;
  }
  return true;
}

CPUPerformanceMesure::~CPUPerformanceMesure() {
    PAPI_cleanup_eventset(EventSet);
    PAPI_destroy_eventset(&EventSet);
    PAPI_shutdown();
}

#endif

#ifdef __USE_CUDA__
size_t getNbBytes(nvinfer1::DataType t, int64_t vol) noexcept
{
    switch (t)
    {
    case nvinfer1::DataType::kINT64: return 8 * vol;
    case nvinfer1::DataType::kINT32:
    case nvinfer1::DataType::kFLOAT: return 4 * vol;
    case nvinfer1::DataType::kBF16:
    case nvinfer1::DataType::kHALF: return 2 * vol;
    case nvinfer1::DataType::kBOOL:
    case nvinfer1::DataType::kUINT8:
    case nvinfer1::DataType::kINT8: return vol;
    case nvinfer1::DataType::kFP8:
#if CUDA_VERSION < 11060
        assert(false && "FP8 is not supported");
#else
        return vol;
#endif
    case nvinfer1::DataType::kINT4:
    case nvinfer1::DataType::kFP4: return (vol + 1) / 2;
    }
    assert(false && "Unknown element type");
}

int64_t volume(nvinfer1::Dims const& dims, int32_t start, int32_t stop)
{
    assert(start >= 0);
    assert(start <= stop);
    assert(stop <= dims.nbDims);
    assert(std::all_of(dims.d + start, dims.d + stop, [](int32_t x) { return x >= 0; }));
    return std::accumulate(dims.d + start, dims.d + stop, int64_t{1}, std::multiplies<int64_t>{});
}

int64_t volume(nvinfer1::Dims const& d)
{
    return std::accumulate(d.d, d.d + d.nbDims, int64_t{1}, std::multiplies<int64_t>{});
}

#endif // __USE_CUDA__

std::wstring to_wstring(const char* c)
{
    const size_t cSize = strlen(c) + 1;

    wchar_t* wc = new wchar_t[cSize];
    mbstowcs(wc, c, cSize);
    std::wstring ws(wc, cSize);
    delete wc;
    return ws;
}

size_t get_feature_id(const String& name, const nlohmann::json& params) {
  auto h1 = std::hash<String>()(name);
  auto h2 = std::hash<nlohmann::json>{}(params);
  h2 ^= h1 + 0x9e3779b9 + (h2 << 6) + (h2 >> 2);
  return h2;
}
