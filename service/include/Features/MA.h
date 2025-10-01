#pragma once
#include "Feature.h"
#include "Scaler.h"
#include "json.hpp"


class EMAFeature: public PrimitiveFeature {
public:
    EMAFeature(const nlohmann::json& params);
    ~EMAFeature();

    virtual bool plug(Server* handle, const String& account);

    virtual bool deal(const QuoteInfo& quote, feature_t&);

    virtual const char* desc();

    virtual FeatureType type() { return FeatureType::FT_EMA; }

    static constexpr StringView name() {
        return "EMA";
    }

private:
    short _N = 12;
    float _alpha = 1;
    float _beta = 1;
    double _prevVal = 0;
};

struct symbol_t;
class MACDFeature: public PrimitiveFeature {
public:
    MACDFeature(const nlohmann::json& params);
    ~MACDFeature();

    virtual bool plug(Server* handle, const String& account);

    virtual bool deal(const QuoteInfo& quote, feature_t&);

    virtual const char* desc();

    virtual FeatureType type() { return FeatureType::FT_MACD; }

    static constexpr StringView name() {
        return "MACD";
    }

private:
    Server* _handle;

    short _signalPeriod = 9;
    short _fastPeriod = 12;
    short _slowPeriod = 26;

    double _signalEma = 0;
    double _fastEma = 0;
    double _slowEma = 0;
    
    double _prevDEA = 0;
    Map<symbol_t, List<double>> _symbolHistory;
};
