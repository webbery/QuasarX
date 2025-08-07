#include "Handler/PredictionHandler.h"
#include "json.hpp"
#include "Algorithms/Random.h"
#include "server.h"
#include <cmath>
#include <ctime>
#include <numeric>
#include "BrokerSubSystem.h"

void MonteCarloHandler::post(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);
    size_t times = params["times"];
    String symbol = params["symbol"];
    time_t start = params["start"];
    int type = params["type"];
    int N = params["N"];
    float dt = params["dt"];
    int reply = params["reply"];
    DataFrequencyType dft = DataFrequencyType::Day;
    if (type == 1) {
        dft = DataFrequencyType::Min5;
    }
    auto datagroup = _server->PrepareStockData({symbol},  dft);
    if (!datagroup->IsValid()) {
        res.status = 500;
        return;
    }
    auto size = datagroup->Size(symbol);
    if (size == 0) {
        res.status = 501;
        return;
    }

    ContractType deriviative = ContractType::AStock;
    double execution = 0;
    if (params.count("derivative")) {
        deriviative = params["derivative"];
        execution = params["E"];
    }
    constexpr double year = 60*60*24*252;
    dt = dt/year;
    auto returns = datagroup->Return(symbol, 0);
    double sum = std::accumulate(returns.begin() + 1, returns.end(), 0.0);
    double mean = sum / (size - 1);
    auto sigma = datagroup->Sigma(symbol);
    int start_index = 0;
    double start_price = 0;
    double freerate = 0;
    for (; start_index < size; ++start_index) {
        time_t t = datagroup->Get<time_t>(symbol, "datetime", start_index);
        if (t >= start) {
            freerate = _server->GetInterestRate(t);
            start_price = datagroup->Get<double>(symbol, "close", start_index);
            break;
        }
    }
    size_t end_index = start_index + N;

    nlohmann::json paths;
    List<double> final_prices;
    for (size_t cnt = 0; cnt < times; ++cnt) {
        auto rands = gauss_noise_simd(0, 1, N);
        double cur_price = start_price;
        List<double> price_path{cur_price};
        for (size_t i = start_index; i < end_index; ++i) {
            int index = i - start_index;
            cur_price = cur_price + (mean + freerate) * cur_price * dt + sigma * cur_price * sqrt(dt) * rands[index];
            price_path.emplace_back(cur_price);
        }
        final_prices.push_back(price_path.back());
        paths["paths"].emplace_back(std::move(price_path));
    }
    sum = std::accumulate(final_prices.begin(), final_prices.end(), 0.0);
    paths["expected"] = sum/final_prices.size();

    double discount = std::exp(-freerate*times*dt);
    double call = 0, put = 0;
    switch (deriviative) {
    case ContractType::AmericanOption: [[likely]]
    break;
    case ContractType::BarrierOption:
    break;
    case ContractType::EureanOption:
        for (auto p: final_prices) {
            call += std::max(0., p - execution);
            put += std::max(0., execution - p);
        }
    break;
    case ContractType::BinaryOption:
        for (auto p: final_prices) {
            call += (p - execution > 0? 1: 0);
            put += (execution - p > 0? 1: 0);
        }
    default:
    break;
    }
    
    if (call > 0) {
        double cm = call/final_prices.size();
        double pm = put/final_prices.size();
        double pv_call = discount*cm;
        double pv_put = discount*pm;
        paths["call"] = pv_call;
        paths["put"] = pv_put;
    }

    res.status = 200;
    res.set_content(paths.dump(), "application/json");
}

void FiniteDifferenceHandler::post(const httplib::Request& req, httplib::Response& res) {
    
}

void PredictionHandler::put(const httplib::Request& req, httplib::Response& res) {
    // 设置预测值
    auto params = nlohmann::json::parse(req.body);
    String symbol = params["symbol"];
    String datetime = params["datetime"];
    int op = params["operation"];
    int exchange = params["exchange"];
    auto next_t = FromStr(datetime);
    if (exchange == 0) {
        auto virtualSystem = _server->GetVirtualSubSystem();
        auto symb = to_symbol(symbol);
        auto cur = Now();

        std::tm local_tm1 = *std::localtime(&cur);
        std::tm local_tm2 = *std::localtime(&next_t);
        // 计算日期差并返回绝对值
        auto N = local_tm2.tm_mday - local_tm1.tm_mday;
        if (N < 0) {
            WARN("day is {}", N);
            return;
        }
        virtualSystem->PredictWithDays(symb, N, op);
    }
    res.status = 200;
}

void PredictionHandler::get(const httplib::Request& req, httplib::Response& res) {
    // 获取预测值
    auto params = nlohmann::json::parse(req.body);
    int exchange = params["exchange"];
    if (params.contains("symbol")) {

    } else {
        // 获取全部
        if (exchange == 0) {
            auto virtualSystem = _server->GetVirtualSubSystem();
            // virtualSystem->
        }
    }
}

void PredictionHandler::del(const httplib::Request& req, httplib::Response& res) {
    auto params = nlohmann::json::parse(req.body);

}
