#pragma once
#include "HttpHandler.h"
#include "Function/GaussianHMM.h"

class HMMHandler : public HttpHandler {
public:
    using HttpHandler::HttpHandler;

    void get(const httplib::Request& req, httplib::Response& res) override;
    void post(const httplib::Request& req, httplib::Response& res) override;
    void del(const httplib::Request& req, httplib::Response& res) override;

private:
    // 加载行情并按日期对齐构建观测矩阵。矩阵每行对应同一个交易日，
    // 各列是 (symbol, feature) 组合，避免不同 warmup 长度的特征按行号错位。
    bool buildObservations(const nlohmann::json& params,
                           Eigen::MatrixXd& out_obs,
                           Vector<String>& out_dates,
                           Vector<String>& out_featureNames,
                           Vector<String>& out_symbols,
                           String& out_error);

    void handleTrain(const nlohmann::json& params, httplib::Response& res);
    void handlePredict(const nlohmann::json& params, httplib::Response& res);
    void handleDecode(const nlohmann::json& params, httplib::Response& res);
    void handleList(httplib::Response& res);
    void handlePublish(const nlohmann::json& params, httplib::Response& res);
};