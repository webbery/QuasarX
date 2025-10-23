#pragma once
#include "std_header.h"
#include "nng/nng.h"
#include "yas/binary_oarchive.hpp"
#include <cstdint>
#include <cstring>
#include "fmt/core.h"
#ifdef __linux__
#include <papi.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
#ifdef __USE_CUDA__
#include "NvInfer.h"
#endif
#ifdef __linux__
#pragma GCC diagnostic pop
#endif
#include <boost/container_hash/hash.hpp>

#define URI_RAW_QUOTE   "inproc://URI_RAW_QUOTE"
#define URI_SIM_QUOTE   "inproc://URI_SIM_QUOTE"// 仿真数据
#define URI_SIM_TRADE   "inproc://URI_SIM_TRADE"    // 仿真交易结果
#define URI_TRADE       "inproc://URI_TRADE"        // 实盘交易结果

#define URI_FEATURE     "inproc://Feature"          // 输出特征
#define URI_PREDICT     "inproc://Predict"          // 输出预测结果
#define URI_SERVER_EVENT "inproc://SSE"             // SSE

constexpr std::size_t flags = yas::mem|yas::binary;

typedef std::tuple<std::string, double, double, double, double, double, double, double> StockRowInfo;

std::string GetIP();

bool RunCommand(const std::string& cmd);
bool RunCommand(const std::string& cmd, String& output);
std::vector<StockRowInfo> ReadCSV(const std::string& csv, int last_N);
std::string GetProgramPath();

bool Subscribe(const std::string& uri, nng_socket& sock, short tick = 5000, short hwm = 64);

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

namespace boost {
    template<>
    struct hash<symbol_t> {
        size_t operator()(const symbol_t& p) const {
            return *(size_t*)&p;
        }
    };
}

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

template <>
struct fmt::formatter<symbol_t> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin(); // 简单情况直接返回
    }

    template <typename FormatContext>
    auto format(symbol_t symbol, FormatContext& ctx) const {
        return fmt::format_to(ctx.out(), "{}", get_symbol(symbol));
    }
};

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

#ifndef FMT_EXCHANGE_NAME
#define FMT_EXCHANGE_NAME(type) case ExchangeName::type: return fmt::format_to(ctx.out(), #type)
#endif

template <>
struct fmt::formatter<ExchangeName> {
    // 解析格式说明符（可选）
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin(); // 简单情况直接返回
    }

    // 必须实现的格式化函数
    template <typename FormatContext>
    auto format(ExchangeName ex, FormatContext& ctx) const {
        switch (ex) {
        FMT_EXCHANGE_NAME(MT_Beijing);
        FMT_EXCHANGE_NAME(MT_Shanghai);
        FMT_EXCHANGE_NAME(MT_ShanghaiFuture);
        FMT_EXCHANGE_NAME(MT_Shenzhen);
        FMT_EXCHANGE_NAME(MT_Hongkong);
        FMT_EXCHANGE_NAME(MT_Zhengzhou);
        FMT_EXCHANGE_NAME(MT_Dalian);
        FMT_EXCHANGE_NAME(MT_Zhongjin);
        FMT_EXCHANGE_NAME(MT_Guangzhou);
        FMT_EXCHANGE_NAME(MT_ShanghaiEng);
        default:
        return fmt::format_to(ctx.out(), "don't know how to convert type {}", (int)ex);
        }
        
    }
};

#ifdef __linux__
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
#endif

#ifdef __USE_CUDA__
size_t getNbBytes(nvinfer1::DataType t, int64_t vol) noexcept;
int64_t volume(nvinfer1::Dims const& dims, int32_t start, int32_t stop);
int64_t volume(nvinfer1::Dims const& d);

template <typename AllocFunc, typename FreeFunc>
class GenericBuffer {
public:
    GenericBuffer(nvinfer1::DataType type = nvinfer1::DataType::kFLOAT)
        : mSize(0)
        , mCapacity(0)
        , mType(type)
        , mBuffer(nullptr)
    {
    }

    GenericBuffer(size_t size, nvinfer1::DataType type)
        : mSize(size)
        , mCapacity(size)
        , mType(type)
    {
        if (!allocFn(&mBuffer, this->nbBytes()))
        {
            throw std::bad_alloc();
        }
    }

    GenericBuffer(GenericBuffer&& buf)
        : mSize(buf.mSize)
        , mCapacity(buf.mCapacity)
        , mType(buf.mType)
        , mBuffer(buf.mBuffer)
    {
        buf.mSize = 0;
        buf.mCapacity = 0;
        buf.mType = nvinfer1::DataType::kFLOAT;
        buf.mBuffer = nullptr;
    }

    GenericBuffer& operator=(GenericBuffer&& buf)
    {
        if (this != &buf)
        {
            freeFn(mBuffer);
            mSize = buf.mSize;
            mCapacity = buf.mCapacity;
            mType = buf.mType;
            mBuffer = buf.mBuffer;
            // Reset buf.
            buf.mSize = 0;
            buf.mCapacity = 0;
            buf.mBuffer = nullptr;
        }
        return *this;
    }

    ~GenericBuffer()
    {
        freeFn(mBuffer);
    }

    void* data()
    {
        return mBuffer;
    }
    const void* data() const
    {
        return mBuffer;
    }

    size_t size() const
    {
        return mSize;
    }

    void resize(size_t newSize)
    {
        mSize = newSize;
        if (mCapacity < newSize)
        {
            freeFn(mBuffer);
            if (!allocFn(&mBuffer, this->nbBytes()))
            {
                throw std::bad_alloc{};
            }
            mCapacity = newSize;
        }
    }

    size_t nbBytes() const
    {
        return getNbBytes(mType, size());
    }

    void resize(const nvinfer1::Dims& dims)
    {
        return this->resize(volume(dims));
    }
private:
    size_t mSize{0}, mCapacity{0};
    nvinfer1::DataType mType;
    void* mBuffer;
    AllocFunc allocFn;
    FreeFunc freeFn;
};

class DeviceAllocator
{
public:
    bool operator()(void** ptr, size_t size) const
    {
        return cudaMalloc(ptr, size) == cudaSuccess;
    }
};

class DeviceFree
{
public:
    void operator()(void* ptr) const
    {
        cudaFree(ptr);
    }
};

class HostAllocator
{
public:
    bool operator()(void** ptr, size_t size) const
    {
        *ptr = malloc(size);
        return *ptr != nullptr;
    }
};

class HostFree
{
public:
    void operator()(void* ptr) const
    {
        free(ptr);
    }
};

using DeviceBuffer = GenericBuffer<DeviceAllocator, DeviceFree>;
using HostBuffer = GenericBuffer<HostAllocator, HostFree>;

struct managed_buffer {
    DeviceBuffer _deviceBuffer;
    HostBuffer _hostBuffer;
};

class BufferManager {
public:
    
};
#endif // __USE_CUDA__

std::wstring to_wstring(const char* c);
