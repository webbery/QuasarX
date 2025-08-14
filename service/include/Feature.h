#pragma once
#include "std_header.h"
#include "Util/log.h"
#include "json.hpp"
#include <variant>

class Server;
struct QuoteInfo;

enum class FeatureType: char {
    FT_PRICE,
    FT_ATR,
    FT_WMA,
    FT_EMA,
    FT_VWAP,
    FT_MACD,

    FT_MIN_MAX_SCALER,
    FT_COUNT,
};

using feature_t = std::variant<double, Vector<float>>;

class alignas(8) IFeature {
public:
    virtual ~IFeature() {}
    /**
     * @brief 同一个特征,参数不同,id也不一样;参数相同的同一类特征,id一样
     */
    virtual size_t id() = 0;

    virtual bool plug(Server* handle, const String& account) = 0;

    virtual feature_t deal(const QuoteInfo& quote, double extra = 0) = 0;

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
    static unsigned short _t;
};


/**
 * 
 */
class PrimitiveFeature: public IFeature {
public:
    virtual FeatureType type() = 0;

protected:
    // static FeatureType _type;
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
