#pragma once
#include "Feature.h"

class RSIFeature: public PrimitiveFeature {
public:
    RSIFeature(const nlohmann::json& params);

    virtual size_t id();

    virtual bool plug(Server* handle, const String& account);

    virtual feature_t deal(const QuoteInfo& quote, double extra = 0);

    virtual const char* desc();

    static constexpr StringView name();

private:
    feature_t calculateRSI() const ;

    feature_t initialize(const QuoteInfo& quote);
    
private:
    short _lastN = 14;
    short _curIdx = 0;
    bool _initialize = false;
    double _prevClose;
    Vector<double> _diffs;

    double _avgGain = 0;
    double _avgLoss = 0;
};

// class RSFeature: public PrimitiveFeature {
// public:
//     RSFeature(const nlohmann::json& params);

//     virtual size_t id();

//     virtual bool plug(Server* handle, const String& account);

//     virtual feature_t deal(const QuoteInfo& quote, double extra = 0);

//     virtual const char* desc();
// };
