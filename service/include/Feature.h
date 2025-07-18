#pragma once
#include "std_header.h"
#include "json.hpp"

class Server;
struct QuoteInfo;

enum class FeatureType: char {
    FT_ATR,
    FT_WMA,
    FT_EMA,
    FT_VWAP,
    FT_COUNT,
};

class alignas(8) IFeature {
public:
    virtual ~IFeature() {}
    /**
     * @brief 同一个特征,参数不同,id也不一样;参数相同的同一类特征,id一样
     */
    // virtual size_t id() = 0;

    virtual bool plug(Server* handle, const String& account) = 0;

    virtual double deal(const QuoteInfo& quote) = 0;

    virtual const char* desc() = 0;
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
