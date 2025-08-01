#pragma once
#include "Feature.h"
#include "json.hpp"

class EMAFeature: public PrimitiveFeature {
public:
    EMAFeature(const nlohmann::json& params);
    ~EMAFeature();

    virtual size_t id();
    
    virtual bool plug(Server* handle, const String& account);

    virtual double deal(const QuoteInfo& quote, double extra = 0);

    virtual const char* desc();

    virtual FeatureType type() { return FeatureType::FT_EMA; }

    static constexpr StringView name() {
        return "EMA";
    }


private:
    short _N = 12;
    float _alpha = 1;
    float _beta = 1;
    double _prev = 0;
};

struct symbol_t;
class MACDFeature: public PrimitiveFeature {
public:
    MACDFeature(const nlohmann::json& params);
    ~MACDFeature();

    virtual size_t id();
    
    virtual bool plug(Server* handle, const String& account);

    virtual double deal(const QuoteInfo& quote, double extra = 0);

    virtual const char* desc();

    virtual FeatureType type() { return FeatureType::FT_MACD; }

    static constexpr StringView name() {
        return "MACD";
    }

private:
    Server* _handle;

    short _signalPeriod;
    short _fastPeriod;
    short _slowPeriod;
    
    Map<symbol_t, Vector<double>> _symbolHistory;
};
