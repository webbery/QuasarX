#include "Handler/RegressionHandler.h"
#include "Util/Spec/regression_spec.h"

namespace {

/// 统一写错误体：{"error": "..."}
///
/// regression.md §6.1 定的错误格式。不要改成 {"message": ...}——
/// parity 用例和将来的前端都按 error 取。
void WriteError(httplib::Response& res, int status, const std::string& message) {
    nlohmann::json body;
    body["error"] = message;
    res.status = status;
    res.set_content(body.dump(), "application/json");
}

}  // namespace

void RegressionHandler::get(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    body["error"] = "请使用 POST；body 为 regression.md §1 定义的 spec";
    body["mode"] = nlohmann::json::array({"cross_section", "time_series", "panel"});
    res.status = 405;
    res.set_header("Allow", "POST");
    res.set_content(body.dump(), "application/json");
}

void RegressionHandler::post(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json spec;
    try {
        spec = nlohmann::json::parse(req.body);
    } catch (const std::exception& e) {
        WriteError(res, 400, std::string("请求体不是合法 JSON: ") + e.what());
        return;
    }

    nlohmann::json result;
    regression_spec::SpecError err;
    if (!regression_spec::Execute(spec, result, err)) {
        WriteError(res, err._status, err._message);
        return;
    }

    res.status = 200;
    res.set_content(result.dump(), "application/json");
}
