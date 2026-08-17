#pragma once
#include "std_header.h"
#include "json.hpp"

namespace httplib { class Request; }

// multipart 请求中提取 script JSON 和 name
bool ParseMultipartScript(const httplib::Request& req, nlohmann::json& scriptJson, String& name, String& errMsg);

// 校验策略 JSON 中每个 XGBoostNode 的 modelFile 匹配 production/{name}-{label}.json
bool ValidateXGBoostModelPaths(const nlohmann::json& scriptJson, const String& strategyName, String& errMsg);

// 从 multipart parts 提取 model_{label} / model_{label}_meta，写到 {dbPath}/models/production/
bool WriteMultipartModelFiles(const httplib::Request& req, const String& strategyName, const String& dbPath, String& errMsg);
