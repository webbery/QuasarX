#pragma once
#include "std_header.h"
#include "nng/nng.h"
#include "yas/binary_oarchive.hpp"
#include <cstdint>
#include <cstring>
#include <papi.h>

#define URI_RAW_QUOTE   "inproc://URI_RAW_QUOTE"
#define URI_SIM_QUOTE   "inproc://URI_SIM_QUOTE"// 仿真数据
// #define URI_QUOTE       "inproc://URI_QUOTE"    // 处理异常数据后的行情
#define URI_SIM_TRADE   "inproc://URI_SIM_TRADE"    // 仿真交易结果
#define URI_TRADE       "inproc://URI_TRADE"        // 实盘交易结果

#define URI_FEATURE     "inproc://Feature"          // 输出特征
#define URI_PREDICT     "inproc://Predict"          // 输出预测结果

typedef std::tuple<std::string, double, double, double, double, double, double, double> StockRowInfo;

std::string GetIP();

bool RunCommand(const std::string& cmd);
std::vector<StockRowInfo> ReadCSV(const std::string& csv, int last_N);

bool Subscribe(const std::string& uri, nng_socket& sock, short tick = 5000);

bool Publish(const std::string& uri, nng_socket& sock);

struct QuoteInfo;
struct symbol_t;
struct DataFeatures;
bool ReadQuote(nng_socket& sock, QuoteInfo& quote, const Set<symbol_t>& filter = {});
bool ReadFeatures(nng_socket& sock, DataFeatures& features);

enum class SIMDSupport {
    None = 0,
    MMX = 1,
    SSE = 0x2,
    SSE2 = 0x4,
    SSE3,
    SSE4,
    SSE4_1,
    SSE4_2,
    AES,
    AVX,
    AVX512,
    XSAVE,
    CUDA,
};

SIMDSupport GetSIMDSupport();
void SetCurrentThreadName(const char* name);

enum ExchangeName: char {
    MT_Unknow = 0,
    MT_Shenzhen,
    MT_Shanghai,
    MT_Beijing,
    MT_Zhengzhou,
    MT_Dalian,
    MT_Zhongjin,
    MT_Guangzhou,
    MT_ShanghaiFuture,
    MT_ShanghaiEng,
    MT_Hongkong,
};

enum class contract_type: char {
    stock = 0,
    future = 1,
    put = 2,
    call = 3,
    fund = 4,
    index = 5,
};

struct alignas(4) symbol_t {
    /**
    * 0 - stock, 1-future, 2- put option, 3- call option 4- fund 5- index 6- BTC
     */
    contract_type _type : 4;
    char _opt: 8;
    char _exchange:8;
    union {
        struct { // option info
            uint32_t _year : 6;
            uint32_t _month : 4;
            uint32_t _price : 10; // unit is 100
        };
        uint32_t _symbol : 20;
    };
};

bool operator==(const symbol_t& , const symbol_t&);
bool operator<(const symbol_t& , const symbol_t&);

namespace yas {
template<typename Archive>
void serialize(Archive& ar, symbol_t& s) {
    if constexpr (yas::is_readable_archive<Archive>::value) { // 反序列化
        uint64_t v = 0;
        ar & v;
        memcpy(&s, &v, sizeof(symbol_t));
    } else {
        uint64_t v = 0;
        memcpy(&v, &s, sizeof(symbol_t));
        ar & v;
    }
}
}
symbol_t to_symbol(const String& symbol, const String& exchange = "");
String get_symbol(const symbol_t&);

bool is_future(symbol_t );
bool is_stock(symbol_t );
bool is_option(symbol_t );
bool is_fund(symbol_t );

struct position_t {
  symbol_t _symbol;
  double _price;
  uint32_t _holds;
  time_t _buy;
  time_t _sell;
};

struct AccountPosition {
  List<position_t> _positions;
};

Set<symbol_t> get_holds(const AccountPosition& account);


class CPUPerformanceMesure {
public:
  CPUPerformanceMesure();

  bool RegistEvents(const Vector<int>& events);

  bool Start();

  bool Stop(Vector<long long>& values);

  ~CPUPerformanceMesure();

private:
  int EventSet = PAPI_NULL;
};
