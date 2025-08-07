#pragma once
#include "std_header.h"
#include "Util/system.h"
#include "DataSource.h"

enum class AgentType: char {
    Unknow,
    LinearRegression,
    XGBoost,
    NeuralNetwork,
    Count
};

struct Signal {
    symbol_t _symbol;
    int16_t _hold;  // positive/negtive
    YAS_DEFINE_STRUCT_SERIALIZE("Signal", _symbol, _hold);
};

class IAgent {
public:
    virtual ~IAgent() {}
    virtual int predict(const DataFeatures& data, Vector<float>& result) = 0;
    virtual void train(const Vector<float>& data, short n_samples, short n_features, const Vector<float>& label, unsigned int epoch) = 0;

protected:
    bool is_valid(const DataFeatures& features) {
        if (!_isValid) {
            if (features._features.size() != _features.size())
                return false;

            auto itr = _features.begin();
            for (auto ft: features._features) {
                auto id = std::hash<StringView>()(*itr);
                if (id != ft)
                    return false;
            }
            _isValid = true;
        }
        return true;
    }

protected:
    bool _isValid = false;
    List<String> _features;
};