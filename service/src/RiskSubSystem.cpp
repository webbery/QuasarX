#include "RiskSubSystem.h"
#include "Risk/StopLoss.h"
#include "Util/system.h"
#include "server.h"

RiskSubSystem::RiskSubSystem(Server* handle):_handle(handle) {

}

RiskSubSystem::~RiskSubSystem() {
    for (auto& item: _stoploss) {
        delete item.second;
    }
    _stoploss.clear();
}

bool RiskSubSystem::Init(nlohmann::json& config) {
    for (auto& item: config) {
        // 止损设置
        String email = item["email"];
        auto& targets = item["target"];
        for (auto& target: targets) {
            String name = target["symbol"];
            auto exc = _handle->GetExchange(name);
            auto ct = _handle->GetContractType(name);
            auto s = to_symbol(name);
            double last_price = target["price"];
            StopLossInfo info;
            if (target.contains("percent")) {
                IStopLoss* sl = new SLPercentage();
                _stoploss[s] = sl;
            }
        }
    }
    return true;
}

void RiskSubSystem::UpdateRisk(IRiskMetric* risk) {

}

void RiskSubSystem::Start() {

}

void RiskSubSystem::run() {
    // 获取持仓标的
    auto& position = _handle->GetPosition();
    // 获取最新数据
    auto holds = get_holds(position);
    // 预估风险敞口
    SetCurrentThreadName("RiskSystem");
    if (!Subscribe(URI_RAW_QUOTE, _sock)) {
        return;
    }

    constexpr std::size_t flags = yas::mem | yas::binary;
    while (!_handle->IsExit()) {
        QuoteInfo quote;
        if (!ReadQuote(_sock, quote)) {
            continue;
        }

        // for (auto& sl : _stloss_map) {
        //     List<symbol_t> sells;
        //     Map<symbol_t, QuoteInfo> tickers{{quote._symbol, quote}};
        //     sl.second->check(tickers, std::move(sells));

        //     if (!sells.empty()) {
        //         // TODO: sell symbols or send to porfolio mananger to make decision

        //         SendEmail(false, sells);
        //         // 
        //         sl.second->del(sells);
        //     }
        // }
    }
    nng_close(_sock);
    _sock.id = 0;
}
