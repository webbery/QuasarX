#pragma once
#include "json.hpp"

class IScaler {
public:
    virtual ~IScaler() {}
};

class MinMaxScaler: public IScaler {
public:
    MinMaxScaler(const nlohmann::json& params);
    ~MinMaxScaler();

private:
};

IScaler* CreateScaler(const std::string& name, const nlohmann::json& args);
