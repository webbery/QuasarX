#pragma once

#include "HttpHandler.h"

#define API_SHIBOR "/market/shibor"

class ShiborHandler: public HttpHandler {
public:
    ShiborHandler(Server* server);
    virtual void get(const httplib::Request& req, httplib::Response& res);

private:
    // 缓存文件路径
    String GetShiborCsvPath();
    
    // 从 CSV 读取数据
    bool ReadCsvData(nlohmann::json& data);
    
    // 获取 CSV 中最新的日期
    String GetLatestCachedDate(const nlohmann::json& data);
    
    // 调用 Python 脚本追加数据到 CSV
    bool AppendFromPython(const String& date);
    
    // 解析查询参数
    String GetQueryParam(const httplib::Request& req, const String& key, const String& default_val = "");
    
    // 过滤数据
    nlohmann::json FilterByDate(const nlohmann::json& data, const String& date);
    nlohmann::json FilterByTerm(const nlohmann::json& data, const String& term);
};
