#include "Bridge/SIM/ETFHistorySimulation.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "server.h"
#include <filesystem>
#include <stdexcept>

ETFHistorySimulation::ETFHistorySimulation(Server* server)
    : HistorySimulationBase(server)
{
}

bool ETFHistorySimulation::Init(const ExchangeInfo& handle) {
    _org_path = handle._quote_addr;

    // 设置 ETF 默认佣金（万 0.5，无最低，无印花税）
    auto [buy, sell] = GetDefaultCommission();
    SetCommission(buy, sell);

    return true;
}

void ETFHistorySimulation::SetEtfCodes(const Set<String>& t0Codes, const Set<String>& t1Codes) {
    _t0Codes = t0Codes;
    _t1Codes = t1Codes;
}

bool ETFHistorySimulation::IsT0(const String& code) const {
    return _t0Codes.count(code) > 0;
}

bool ETFHistorySimulation::LoadData(const String& code) {
    auto& security = Server::GetSecurity(code);
    auto symbol = to_symbol(code, security);

    // 后复权价格路径: etf_hfq/{freq}/{code}.csv
    String adjPath = _org_path + "/etf_hfq/" + _freq + "/" + code + ".csv";
    // 原始价格路径: etf_org/{freq}/{code}.csv
    String orgPath = _org_path + "/etf_org/" + _freq + "/" + code + ".csv";

    if (!LoadCSVToDataFrame(adjPath, _csvs[symbol], _headers[symbol])) {
        String err_msg = fmt::format(
            "Failed to load ETF backtest data for '{}': CSV not found at '{}'",
            code, adjPath);
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }

    // 加载原始价文件
    if (!LoadCSVToDataFrame(orgPath, _org_csvs[symbol], _org_headers[symbol])) {
        WARN("load {} fail, will use adjusted price", orgPath);
        _org_csvs[symbol] = _csvs[symbol];
        _org_headers[symbol] = _headers[symbol];
    }

    return true;
}

std::pair<Commission, Commission> ETFHistorySimulation::GetDefaultCommission() const {
    Commission buy, sell;

    buy._valid = true;
    buy._status = true;
    buy._direction = 0;
    buy._type = 0;
    buy._ration = 0.00005;   // 万 0.5
    buy._min = 0.0;          // 无最低限制
    buy._stamp = 0.0;        // ETF 无印花税

    sell = buy;
    sell._direction = 1;

    return {buy, sell};
}

void ETFHistorySimulation::OnDataLoaded() {
    // 根据 ETF 代码列表配置交易模式
    // 如果 _filter._symbols 中有任何 T0 ETF，使用 T0 模式
    bool hasT0 = false;
    for (auto& code : _filter._symbols) {
        if (IsT0(code)) {
            hasT0 = true;
            break;
        }
    }
    // 如果全部是 T0，则使用 T0 模式；否则使用 T1
    // TODO: 如果需要更精细的控制（混合 T0/T1），可在 BacktestContext 中按 symbol 分别处理
    INFO("[ETF] Data loaded: {} symbols, T0 mode: {}", _filter._symbols.size(), hasT0 ? "yes" : "no");
}
