#pragma once
#include "Feature.h"
#include "json.hpp"

class EMAFeature: public PrimitiveFeature {
public:
    EMAFeature(const nlohmann::json& params);
    ~EMAFeature();

    // virtual size_t id();
    
    virtual bool plug(Server* handle, const String& account);

    virtual double deal(const QuoteInfo& quote);

    virtual const char* desc();

    static constexpr StringView name() {
        return "EMA";
    }

private:
    short _N = 12;
    float _alpha = 1;
    double _prev = 0;
};