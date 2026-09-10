#include "Bridge/SIM/OptionHistorySimulation.h"
#include "Util/OptionDataDB.h"
#include "Util/log.h"
#include "Util/system.h"
#include "server.h"
#include <stdexcept>

OptionHistorySimulation::OptionHistorySimulation(Server* server)
    : HistorySimulationBase(server)
{
}

bool OptionHistorySimulation::Init(const ExchangeInfo& handle) {
    _org_path = handle._quote_addr;

    auto [buy, sell] = GetDefaultCommission();
    SetCommission(buy, sell);

    return true;
}

bool OptionHistorySimulation::LoadData(const String& code) {
    auto symbol = to_symbol(code, "", contract_type::option);
    if (is_null(symbol)) {
        WARN("[Option] Invalid option symbol code '{}'", code);
        return false;
    }

    int64_t symbol_id = 0;
    std::memcpy(&symbol_id, &symbol, sizeof(symbol_t));

    auto& db = OptionDataDB::instance();
    auto result = db.queryBySymbolId(
        symbol_id,
        _loadStartDate,
        _loadEndDate,
        100000  // 足够大的 limit
    );

    if (result.contains("error")) {
        String err = result["error"].get<String>();
        WARN("[Option] Query failed for '{}': {}", code, err);
        throw std::runtime_error("OptionDataDB query failed: " + err);
    }

    int count = result.value("count", 0);
    if (count == 0) {
        String err_msg = fmt::format("No option data for '{}' (symbol_id={})", code, symbol_id);
        WARN("{}", err_msg);
        throw std::runtime_error(err_msg);
    }

    const auto& data = result["data"];

    Map<String, Vector<double>> fieldMap;
    Vector<String> dates;
    dates.reserve(count);
    fieldMap["open"].reserve(count);
    fieldMap["close"].reserve(count);
    fieldMap["high"].reserve(count);
    fieldMap["low"].reserve(count);
    fieldMap["volume"].reserve(count);

    for (int i = 0; i < count; ++i) {
        const auto& row = data[i];
        dates.push_back(row["trade_date"].get<String>());
        fieldMap["open"].push_back(row.value("open", 0.0));
        fieldMap["close"].push_back(row.value("close", 0.0));
        fieldMap["high"].push_back(row.value("high", 0.0));
        fieldMap["low"].push_back(row.value("low", 0.0));
        fieldMap["volume"].push_back(static_cast<double>(row.value("volume", int64_t(0))));
    }

    BuildOHLCVDataFromMap(fieldMap, dates, _csvs[symbol]);
    BuildOHLCVDataFromMap(fieldMap, dates, _org_csvs[symbol]);

    INFO("[Option] Loaded {} bars for '{}'", count, code);
    return true;
}

std::pair<Commission, Commission> OptionHistorySimulation::GetDefaultCommission() const {
    Commission buy, sell;

    buy._valid = true;
    buy._status = true;
    buy._direction = 0;
    buy._type = 0;
    buy._ration = 3.0;       // 每张 3 元（ETF 期权默认佣金）
    buy._min = 0.0;
    buy._stamp = 0.0;        // 期权无印花税

    sell = buy;
    sell._direction = 1;

    return {buy, sell};
}

void OptionHistorySimulation::OnDataLoaded() {
    INFO("[Option] Data loaded: {} symbols, T+0 mode", _filter._symbols.size());
}
