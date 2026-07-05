#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Bridge/exchange.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
#include "Util/data.h"
#include "std_header.h"
#include <algorithm>
#include <exception>
#include <filesystem>
#include <utility>
#include "server.h"
#include "BrokerSubSystem.h"

StockHistorySimulation::StockHistorySimulation(Server* server)
    : HistorySimulationBase(server), _tradingMode(TradingMode::T1)
{
}

StockHistorySimulation::~StockHistorySimulation() {
}

bool StockHistorySimulation::Init(const ExchangeInfo& handle) {
    HistorySimulationBase::Init(handle);

    // 默认佣金配置（股票）: 佣金费率 0.01345%, 最低 5 元, 印花税 0.1%
    auto [buy, sell] = GetDefaultCommission();
    SetCommission(buy, sell);

    return true;
}

bool StockHistorySimulation::GetPosition(AccountPosition& pos) {
    return true;
}

AccountAsset StockHistorySimulation::GetAsset() {
    AccountAsset ass;
    return ass;
}

std::pair<Commission, Commission> StockHistorySimulation::GetDefaultCommission() const {
    Commission buy, sell;

    buy._valid = true;
    buy._status = true;
    buy._direction = 0;  // BUY
    buy._type = 0;       // 按金额收取比例
    buy._ration = 0.0001345;
    buy._min = 5.0;
    buy._stamp = 0.001;  // 买入印花税（A 股买入不收，但预留字段）

    sell = buy;
    sell._direction = 1; // SELL

    return {buy, sell};
}

void StockHistorySimulation::OnDataLoaded() {
    INFO("[Stock] Data loaded: {} symbols, TradingMode: {}",
         _filter._symbols.size(),
         _tradingMode == TradingMode::T0 ? "T0" : "T1");
}

bool StockHistorySimulation::LoadData(const String& code) {
    auto& security = Server::GetSecurity(code);
    auto symbol = to_symbol(code, security);

    // 确定频率：T1 → 日线，T0 → 分钟级
    BarFreq freq = (_tradingMode == TradingMode::T1) ? BarFreq::Day : parseBarFreq(_t0Freq.empty() ? "1m" : _t0Freq);

    // 后复权数据（指标计算）
    Vector<String> adjDates;
    auto adjData = LoadHistoryDataWithFreq(
        symbol, {"open", "close", "high", "low", "volume"},
        "", "", freq, AdjType::HFQ, &adjDates);

    if (adjData.empty()) {
        String err_msg = fmt::format("No stock data for '{}' from DuckDB (freq={}, adj=HFQ)", code, toString(freq));
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }

    BuildDataFrameFromMap(adjData, adjDates, _csvs[symbol], _headers[symbol]);

    // 原始价数据（撮合）
    Vector<String> orgDates;
    auto orgData = LoadHistoryDataWithFreq(
        symbol, {"open", "close", "high", "low", "volume"},
        "", "", freq, AdjType::None, &orgDates);

    if (orgData.empty()) {
        String err_msg = fmt::format("No stock data for '{}' from DuckDB (freq={}, adj=None)", code, toString(freq));
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }

    BuildDataFrameFromMap(orgData, orgDates, _org_csvs[symbol], _org_headers[symbol]);

    return true;
}

std::pair<std::vector<time_t>, std::vector<double>> StockHistorySimulation::GetHFQCloseData(symbol_t symbol) const {
    std::vector<time_t> datetimes;
    std::vector<double> closes;

    auto itr = _csvs.find(symbol);
    if (itr == _csvs.end()) {
        return {datetimes, closes};
    }

    const auto& df = itr->second;
    const auto& header = _headers.at(symbol);
    if (header.empty()) {
        return {datetimes, closes};
    }

    const auto& datetime_col = df.get_column<time_t>(header[0].c_str());
    const auto& close_col = df.get_column<float>(header[2].c_str());

    datetimes.reserve(datetime_col.size());
    closes.reserve(close_col.size());

    for (size_t i = 0; i < datetime_col.size(); ++i) {
        datetimes.push_back(datetime_col[i]);
        closes.push_back(static_cast<double>(close_col[i]));
    }

    return {datetimes, closes};
}
