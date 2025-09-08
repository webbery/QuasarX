#include "Features/Scaler.h"
#include "Util/log.h"

MinMaxScaler::MinMaxScaler(const nlohmann::json& params) {

}
MinMaxScaler::~MinMaxScaler() {

}

IScaler* CreateScaler(const std::string& name, const nlohmann::json& params) {
    if (name == "minmax") {
        nlohmann::json args;
        if (params.contains("args")) {
            args = params["args"];
        }
        return new MinMaxScaler(args);
    } else {
        WARN("not support {} scaler.", name);
        return nullptr;
    }
}
