#pragma once
#include "std_header.h"
#include "json.hpp"
#include "Features/Scaler.h"
#include <cstddef>

class Server;
struct QuoteInfo;

enum class FeatureType: char {
    FT_PRICE,
    FT_ATR,
    FT_WMA,
    FT_EMA,
    FT_VWAP,
    FT_MACD,

    FT_OPEN,
    FT_CLOSE,
    FT_HIGH,
    FT_LOW,
    FT_VOLUME,
    FT_TURNOVER,

    FT_MIN_MAX_SCALER,
    FT_COUNT,
};

class alignas(8) IFeature {
public:
    virtual ~IFeature() {}
    /**
     * @brief 同一个特征,参数不同,id也不一样;参数相同的同一类特征,id一样
     */
    size_t id() { return _id; }

    virtual bool plug(Server* handle, const String& account) = 0;

    virtual bool deal(const QuoteInfo& quote, feature_t& output) = 0;

    virtual const char* desc() = 0;

protected:
    bool check(const nlohmann::json& params, const String& prop) {
        if (params.contains(prop) == 0) {
            WARN("params do not contain property `{}`", prop);
            return false;
        }
        return true;
    }
protected:
    size_t _id;
};

size_t get_feature_id(const String& name, const nlohmann::json& params);
/**
 * 
 */
class PrimitiveFeature: public IFeature {
public:
    virtual FeatureType type() = 0;
    ~PrimitiveFeature() {
        if (_scaler) {
            delete _scaler;
        }
    }

    bool isValid(const QuoteInfo& q);
    
protected:
    // 上一次数据的时间戳,如果同一个时间戳说明数据相同，不需要再次计算
    time_t _last = 0;
    feature_t _prevs;
    IScaler* _scaler = nullptr;
};

template<typename... T>
class TypeFactory;

template <>
class TypeFactory<> {
public:
    static IFeature* Create(const StringView& , const nlohmann::json&) { return nullptr; }
};

template <typename T, typename... Rest>
class TypeFactory<T, Rest...>: TypeFactory<Rest...> {
public:
    static IFeature* Create(const StringView& name, const nlohmann::json& params) {
        if (name == T::name()) {
            return new T(params);
        }
        return TypeFactory<Rest...>::Create(name, params);
    }
};
