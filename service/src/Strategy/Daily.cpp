#include "Strategy/Daily.h"
#include "server.h"

DailyStrategy::DailyStrategy(Server* handle, const nlohmann::json& params): _handle(handle) {

}

int DailyStrategy::generate(const Vector<float>& prediction) {
    if (prediction.size() == 1) {
        if (prediction[0] > 0.65) {
            return (int)ContractOperator::Sell;
        }
        else if (prediction[0] < 0.3) {
            return (int)ContractOperator::Long;
        }
    }
    else if (prediction.size() == 2) {
        if (prediction[0] > 0.65) {
            return (int)ContractOperator::Sell;
        }
        if (prediction[0] < 0.3) {
            return (int)ContractOperator::Long;
        }
    }
    return 0;
}

bool DailyStrategy::is_valid() {
    // only support stock
    return !_handle->IsOpen(ExchangeName::MT_Shanghai, Now());
}
