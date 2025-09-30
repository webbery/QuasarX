#pragma once
#include "Feature.h"

class ATRFeature: public PrimitiveFeature {
public:
    ATRFeature(const nlohmann::json& params);
    ~ATRFeature();
    virtual bool plug(Server* handle, const String& account);

    virtual bool deal(const QuoteInfo& quote, feature_t&);

    virtual const char* desc() { return "ATR"; }

    virtual FeatureType type() { return FeatureType::FT_ATR; }

    static constexpr String name() { return "ATR"; }

private:
    double tr(short index);
private:
    short _T = 10;
    unsigned short _cur = 0;
    unsigned short _cnt = 0;
    double* _close = nullptr;
    Vector<double> _tr;
    double _sum;
};
