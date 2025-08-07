#include "Agents/XGBoostAgent.h"
#include <filesystem>

XGBoostAgent::XGBoostAgent(const String& path, int classes, const nlohmann::json& params)
:_modelpath(path), _params(params), _classes(classes) {
    auto code = XGBoosterCreate(nullptr, 0, &_booster);
    if (code) {
        FATAL("create booster fail[{}]: {}", code, XGBGetLastError());
        return;
    }
    if (!path.empty() && std::filesystem::exists(path)) {
        if (XGBoosterLoadModel(_booster, path.c_str())) {
            FATAL("load model {} fail: {}", path, XGBGetLastError());
            return;
        }

        bst_ulong out_len;
        const char ** out_str;
        XGBoosterGetStrFeatureInfo(_booster, "feature_name", &out_len, &out_str);
        if (out_len == 0) {
            FATAL("model feature is zero");
            return;
        }
        for (int i = 0; i < out_len; ++i) {
            const char* attr_name = out_str[i];
            _features.push_back(attr_name);
        }
    }
}

int XGBoostAgent::predict(const DataFeatures& data, Vector<float>& result) {
    if (is_valid(data))
        return -1;

    DMatrixHandle xgb = nullptr;
    if (XGDMatrixCreateFromMat(data._data.data(), 1, _features.size(), 0.0f, &xgb)) {
        FATAL("XGDMatrixCreateFromMat fail: {}", XGBGetLastError());
        return -1;
    }
    bst_ulong out_dim;
    const char* config = "{\"type\": 0,\"training\": false,\"iteration_begin\": 0,\"iteration_end\": 0,\"strict_shape\": true}";
    const bst_ulong* out_shape;
    const float* out_result;
    if (XGBoosterPredictFromDMatrix(_booster, xgb, config, &out_shape, &out_dim, &out_result)) {
        FATAL("XGBoosterPredict fail: {}", XGBGetLastError());
        XGDMatrixFree(xgb);
        return -1;
    }

    // send result to waiter
    result.resize(out_dim);
    memcpy(&result[0], out_result, out_dim*sizeof(float));
    XGDMatrixFree(xgb);

    return 0;
}

void XGBoostAgent::train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) {
    DMatrixHandle* xgb = nullptr;
    XGDMatrixCreateFromMat(data.data(), n_samples, n_features, -1, xgb);
    XGDMatrixSetFloatInfo(xgb, "label", label.data(), label.size());
    // train params
    for (auto& param: _params.items()) {
        String key = param.key();
        String val = param.value();
        XGBoosterSetParam(_booster, key.c_str(), val.c_str());
    }

    for (uint32_t i = 0; i < epoch; ++i) {
        XGBoosterUpdateOneIter(_booster, i, xgb);
    }

    XGDMatrixFree(xgb);
}

XGBoostAgent::~XGBoostAgent() {
    if (_booster) XGBoosterFree(_booster);
}
