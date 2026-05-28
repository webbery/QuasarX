#include "Bridge/SIM/StockHistorySimulation.h"
#include "Bridge/SIM/BacktestContext.h"
#include "Bridge/exchange.h"
#include "DataFrame/DataFrameTypes.h"
#include "Util/datetime.h"
#include "Util/log.h"
#include "Util/string_algorithm.h"
#include "Util/system.h"
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
    if (_tradingMode == TradingMode::T1) {
        LoadT1(code);
    } else {
        LoadT0(code);
    }
    return true;
}

void StockHistorySimulation::LoadT1(const String& code) {
    auto& security = Server::GetSecurity(code);
    auto symbol = to_symbol(code, security);
    String subdir, orgdir;
    if (is_stock(symbol)) {
        subdir = "A_hfq";
        orgdir = "AStock";
    }
    auto file_path = _org_path + "/" + subdir + "/" + code + ".csv";
    auto primitive_file_path = _org_path + "/" + orgdir + "/" + code + ".csv";

    if (!LoadCSVToDataFrame(file_path, _csvs[symbol], _headers[symbol])) {
        String err_msg = fmt::format("Failed to load backtest data for '{}': CSV file not found or invalid at '{}'. "
                                     "Please ensure the code format is correct (e.g., 'sh.600519' or 'sz.000001') "
                                     "and the CSV file exists in the data directory.", code, file_path);
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }

    if (!LoadCSVToDataFrame(primitive_file_path, _org_csvs[symbol], _org_headers[symbol])) {
        WARN("load {} fail, will use adjusted price", primitive_file_path);
        _org_csvs[symbol] = _csvs[symbol];
        _org_headers[symbol] = _headers[symbol];
    }
}

void StockHistorySimulation::LoadT0(const String& code) {
    auto& security = Server::GetSecurity(code);
    auto symbol = to_symbol(code, security);
    String subdir;
    if (is_stock(symbol)) {
        subdir = "stock";
    }
    auto file_path = _org_path + "/zh/" + subdir + "/" + code + ".csv";

    if (!LoadCSVToDataFrame(file_path, _csvs[symbol], _headers[symbol])) {
        String err_msg = fmt::format("Failed to load backtest data for '{}': CSV file not found or invalid at '{}'. "
                                     "Please ensure the code format is correct (e.g., 'sh.600519' or 'sz.000001') "
                                     "and the CSV file exists in the data directory.", code, file_path);
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }
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
