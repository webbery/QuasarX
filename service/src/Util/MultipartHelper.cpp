#include "Util/MultipartHelper.h"
#include "Util/datetime.h"
#include "Util/string_algorithm.h"
#include "httplib.h"
#include <filesystem>
#include <fstream>

namespace {
std::filesystem::path utf8Path(const String& utf8) {
#ifdef _WIN32
    return std::filesystem::path(utf8_to_utf16(utf8));
#else
    return std::filesystem::path(utf8);
#endif
}
}

bool ParseMultipartScript(const httplib::Request& req, nlohmann::json& scriptJson, String& name, String& errMsg) {
    if (!req.has_file("script")) {
        errMsg = "missing 'script' part in multipart";
        return false;
    }
    String scriptStr = req.get_file_value("script").content;
    try {
        scriptJson = nlohmann::json::parse(scriptStr);
    } catch (const std::exception& e) {
        errMsg = String("invalid 'script' JSON in multipart: ") + e.what();
        return false;
    }
    if (req.has_file("name")) {
        name = req.get_file_value("name").content;
    }
    return true;
}

bool ValidateXGBoostModelPaths(const nlohmann::json& scriptJson, const String& strategyName, String& errMsg) {
    if (!scriptJson.contains("nodes") || !scriptJson["nodes"].is_array()) return true;
    for (const auto& n : scriptJson["nodes"]) {
        if (!n.contains("data") || n["data"].value("nodeType", "") != "xgboost") continue;
        String xgbLabel = n["data"].value("label", "");
        String modelFile = "";
        if (n["data"].contains("params") && n["data"]["params"].contains("modelFile")) {
            const auto& mf = n["data"]["params"]["modelFile"];
            modelFile = mf.is_string() ? mf.get<String>() : mf.value("value", "");
        }
        String expected = "production/" + strategyName + "-" + xgbLabel + ".json";
        if (!modelFile.empty() && modelFile != expected) {
            errMsg = "XGBoostNode '" + xgbLabel + "' modelFile='" + modelFile +
                     "' 与策略 '" + strategyName + "' 不匹配，禁止跨策略引用（应为 '" + expected + "'）";
            return false;
        }
    }
    return true;
}

bool WriteMultipartModelFiles(const httplib::Request& req, const String& strategyName, const String& dbPath, String& errMsg) {
    std::filesystem::path prodDir = utf8Path(dbPath) / "models" / "production";
    try {
        std::filesystem::create_directories(prodDir);
    } catch (const std::filesystem::filesystem_error& e) {
        errMsg = String("Failed to create production dir: ") + e.what();
        return false;
    }

    for (const auto& file : req.files) {
        const String& partName = file.first;
        if (partName.rfind("model_", 0) != 0) continue;
        String labelPart = partName.substr(6);
        // 跳过 _meta 后缀的 part（在主文件处理时一并处理）
        if (labelPart.size() > 5 && labelPart.substr(labelPart.size() - 5) == "_meta") continue;

        String modelFileName = strategyName + "-" + labelPart + ".json";
        String metaFileName = strategyName + "-" + labelPart + ".meta.json";
        std::filesystem::path modelOut = prodDir / utf8Path(modelFileName);
        std::filesystem::path metaOut = prodDir / utf8Path(metaFileName);
        try {
            std::ofstream mofs(modelOut, std::ios::out | std::ios::trunc | std::ios::binary);
            mofs << file.second.content;
            mofs.close();

            String metaPartName = "model_" + labelPart + "_meta";
            if (req.has_file(metaPartName)) {
                std::ofstream mefs(metaOut, std::ios::out | std::ios::trunc | std::ios::binary);
                mefs << req.get_file_value(metaPartName).content;
                mefs.close();
            }
            INFO("[MultipartHelper] Saved model: {} (meta: {})", modelFileName,
                 std::filesystem::exists(metaOut) ? metaFileName : "<none>");
        } catch (const std::exception& e) {
            errMsg = String("Failed to save model '") + modelFileName + "': " + e.what();
            return false;
        }
    }
    return true;
}
