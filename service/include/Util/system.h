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
#include "json.hpp"

#define ERROR_INSERT_LIMIT      -100
#define ERROR_DAILY_LIMIT       -101
#define ERROR_CANCEL_LIMIT      -102
#define ERROR_ORDER_INSERT      -103
#define ERROR_SET_DAILY_LIMIT   -104
#define ERROR_SET_CANCEL_LIMIT  -105
#define ERROR_SET_ORDER_LIMIT   -106
#define ERROR_NO_SECURITY       -107    //无此证券
#define ERROR_REQUIR_TYPE       -108    // 请求缺type参数
#define ERROR_ORDER_FORBID      -109    // 禁止报单

#define URI_RAW_QUOTE   "inproc://URI_RAW_QUOTE"
#define URI_SIM_QUOTE   "inproc://URI_SIM_QUOTE"// 仿真数据
#define URI_SIM_TRADE   "inproc://URI_SIM_TRADE"    // 仿真交易结果
#define URI_TRADE       "inproc://URI_TRADE"        // 实盘交易结果

#define URI_FEATURE     "inproc://Feature"          // 输出特征
#define URI_PREDICT     "inproc://Predict"          // 输出预测结果
#define URI_SERVER_EVENT "inproc://SSE_PULL"        // SSE
#define URI_DISPATH_EVENT "inproc://SSE_PUB"        // SSE

// 校验 JSON 对象中必须包含指定 key，缺失则抛出 std::invalid_argument
#define JSON_REQUIRE_KEY(obj, key) \
    do { \
        if (!(obj).contains(key)) { \
            throw std::invalid_argument("JSON missing required field: '" key "'"); \
        } \
    } while (0)

// 校验 JSON 对象中是否包含指定 key，缺失则 continue（用于循环中的可选字段）
#define JSON_SKIP_IF_MISSING(obj, key) \
    if (!(obj).contains(key)) continue

// 校验 JSON 对象中是否包含指定 key，缺失则 return（用于 void 函数提前返回）
#define JSON_RETURN_IF_MISSING(obj, key) \
    if (!(obj).contains(key)) return

// 校验 JSON 对象中是否包含指定 key，缺失则 return val（用于有返回值的函数提前返回）
#define JSON_RETURN_VAL_IF_MISSING(obj, key, val) \
    if (!(obj).contains(key)) return (val)

constexpr std::size_t flags = yas::mem|yas::binary;

std::string GetIP();
String GetMacAddr();

#ifdef _WIN32
constexpr const char* PYTHON_CMD = "python";
#else
constexpr const char* PYTHON_CMD = "python3";
#endif

bool RunCommand(const std::string& cmd);
bool RunCommand(const std::string& cmd, String& output);

std::string GetProgramPath();

bool Subscribe(const std::string& uri, nng_socket& sock, short tick = 5000, short hwm = 64);

bool Publish(const std::string& uri, nng_socket& sock);

// 接收方，只能一个
bool Puller(const String& uri, nng_socket& sock);
// 发布方(可以多个)
bool Pusher(const String& uri, nng_socket& sock);

struct QuoteInfo;
struct symbol_t;
bool ReadQuote(nng_socket& sock, QuoteInfo& quote, const Set<symbol_t>& filter = {});

// 通过 nng socket 发送 SSE 事件到 /v0/server/event
void SendSSE(const nng_socket& sock, const String& event, const Map<String, String>& data);

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
    MT_COUNT,
};

bool operator==(const symbol_t& , const symbol_t&);
bool operator<(const symbol_t& , const symbol_t&);

template<>
struct boost::hash<symbol_t> {
    size_t operator()(const symbol_t& p) const {
        return *(size_t*)&p;
    }
};
template<>
struct std::hash<symbol_t> {
    size_t operator()(const symbol_t& p) const {
        return *(size_t*)&p;
    }
};

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

struct ContractInfo;
symbol_t to_symbol(const String& symbol, const String& exchange = "", contract_type t=contract_type::stock);
symbol_t to_symbol(const String& code, const ContractInfo& security);
symbol_t null_symbol();

String get_symbol(const symbol_t&);

bool is_future(symbol_t );
bool is_stock(symbol_t );
bool is_etf_option(symbol_t);
bool is_option(symbol_t );
bool is_fund(symbol_t );
bool is_etf(symbol_t );
bool is_null(symbol_t);

// 将 CODE.EXCHANGE 或 BaoStock 格式转为 sh.000001 / sz.159919
std::string toInternalSymbol(const std::string& csv_stem);

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

// 统一持仓结构（合并 position_t 和 StockPosition，支持两融）
// 可被 StockPositionManager、StrategyCapital、PortfolioNode、HXTrade 等多个模块复用
struct Position {
    symbol_t _symbol;
    int64_t _quantity = 0;        // 持仓数量（正=多头，负=空头）
    double _avgPrice = 0.0;       // 平均成本
    double _currentPrice = 0.0;   // 当前价格
    double _totalCost = 0.0;      // 总成本 = _quantity × _avgPrice
    
    // 基础信息
    String _name;                 // 标的名称
    time_t _buyTime = 0;          // 买入时间
    time_t _sellTime = 0;         // 卖出时间
    int64_t _validQuantity = 0;   // 可用量（T+1 控制）
    
    // 两融相关
    bool _isMargin = false;       // 是否保证金交易
    double _marginRatio = 1.0;    // 保证金比例（1.0=全额，0.5=50%保证金）
    int64_t _borrowedShares = 0;  // 借入的股票数量（融券卖出）
    double _borrowedCash = 0.0;   // 借入的现金（融资买入）
    
    // 计算方法
    double getMarketValue() const {
        return _quantity * _currentPrice;
    }
    
    double getUnrealizedPnL() const {
        return _quantity * (_currentPrice - _avgPrice);
    }
    
    double getMarginRequirement() const {
        if (!_isMargin) return std::abs(getMarketValue());
        return std::abs(getMarketValue()) * _marginRatio;
    }
    
    bool isLong() const { return _quantity > 0; }
    bool isShort() const { return _quantity < 0; }
};

// 兼容旧代码的类型别名
using position_t = Position;

// 成交记录（支持两融）
// 表示一笔已完成的交易，用于 StrategyCapital 的成交回写
struct Fill {
    symbol_t _symbol;
    int64_t _quantity = 0;        // 成交数量（正=买入，负=卖出）
    double _price = 0.0;          // 成交价格
    time_t _time = 0;             // 成交时间
    
    // 费用
    double _commission = 0.0;     // 佣金
    double _stampTax = 0.0;       // 印花税（仅卖出）
    double _slippage = 0.0;       // 滑点
    
    // 两融相关
    bool _isMargin = false;       // 是否保证金交易
    double _marginRatio = 1.0;    // 保证金比例（1.0=全额，0.5=50%保证金）
    bool _isShortSell = false;    // 是否融券卖出
    bool _isShortCover = false;   // 是否买入平空
    double _financingRate = 0.0;  // 融资利率（年化，如 0.08 = 8%）
    double _securitiesLendingRate = 0.0; // 融券费率（年化，如 0.10 = 10%）
    
    // 计算方法
    double getGrossAmount() const {
        return std::abs(_quantity) * _price;
    }
    
    double getTotalFees() const {
        return _commission + _stampTax + _slippage;
    }
    
    double getNetAmount() const {
        double gross = getGrossAmount();
        if (_quantity > 0) {
            // 买入：净支出 = 总金额 + 费用
            return -(gross + getTotalFees());
        } else {
            // 卖出：净收入 = 总金额 - 费用
            return gross - getTotalFees();
        }
    }
    
    bool isBuy() const { return _quantity > 0; }
    bool isSell() const { return _quantity < 0; }
};

struct AccountPosition {
  List<Position> _positions;
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

enum class MarketType : char {
    AStock,
    BStock,
    STARMarket, //科创板
    GEMMarkget, //创业板
    ThreeMarket,// 新三板
    BJMarket,   // 北交所
};
// 获取股票的类型
MarketType getMarketType(symbol_t stock);

class RuntimeError : public std::exception {
public:
    RuntimeError(int code):_code(code) {}

private:
    int _code;
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

bool get_system_status(nlohmann::json&);

/// @brief 获取当前进程的 RSS（Resident Set Size），单位 MB
/// Linux 读 /proc/self/status VmRSS，Windows 用 GetProcessMemoryInfo
/// 失败返回 0
double getProcessRSS();

void strategy_error(const String& strategy, const String& info);
void strategy_log(const String & strategy, const String & info);