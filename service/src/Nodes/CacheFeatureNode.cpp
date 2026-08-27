#include "Nodes/CacheFeatureNode.h"
#include "Util/system.h"
#include <fstream>
#include <sstream>

bool CacheFeatureNode::Init(const nlohmann::json& config) {
    _cachePath = (String)config["params"]["cache_path"]["value"];
    if (_cachePath.empty()) {
        WARN("[CacheFeatureNode:{}] cache_path is empty", _id);
        return false;
    }
    if (!LoadCache()) {
        WARN("[CacheFeatureNode:{}] failed to load cache from '{}'", _id, _cachePath);
        return false;
    }
    INFO("[CacheFeatureNode:{}] loaded {} features, {} bars from '{}'",
         _id, _featureNames.size(), _currentBar == 0 ? _data.begin()->second.size() : 0, _cachePath);
    return true;
}

bool CacheFeatureNode::LoadCache() {
    std::ifstream ifs(_cachePath);
    if (!ifs.is_open()) {
        WARN("[CacheFeatureNode:{}] cannot open '{}'", _id, _cachePath);
        return false;
    }

    String line;
    if (!std::getline(ifs, line)) return false;

    // parse header: date,col1,col2,...
    Vector<String> headers;
    {
        std::istringstream ss(line);
        String cell;
        while (std::getline(ss, cell, ',')) {
            headers.push_back(cell);
        }
    }
    if (headers.size() < 2) {
        WARN("[CacheFeatureNode:{}] CSV has no data columns", _id);
        return false;
    }

    // skip "date" column (index 0), rest are feature keys
    size_t firstDataCol = (headers[0] == "date") ? 1 : 0;
    _featureNames.clear();
    _symbols.clear();
    for (size_t i = firstDataCol; i < headers.size(); ++i) {
        _featureNames.push_back(headers[i]);
        _data[headers[i]] = Vector<double>{};

        // 从 key 中提取 symbol（格式 "sz.800001.MA(5)" → exchange="sz", code="800001"）
        const String& key = headers[i];
        auto firstDot = key.find('.');
        if (firstDot != String::npos) {
            auto secondDot = key.find('.', firstDot + 1);
            if (secondDot != String::npos) {
                String strSymbol = key.substr(0, secondDot - firstDot - 1);
                _symbols.insert(to_symbol(strSymbol));
            }
        }
    }

    // parse rows
    size_t rowCount = 0;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue;
        std::istringstream ss(line);
        String cell;
        size_t col = 0;
        while (std::getline(ss, cell, ',')) {
            if (col >= firstDataCol && (col - firstDataCol) < _featureNames.size()) {
                try {
                    _data[_featureNames[col - firstDataCol]].push_back(std::stod(cell));
                } catch (...) {
                    _data[_featureNames[col - firstDataCol]].push_back(0.0);
                }
            }
            ++col;
        }
        ++rowCount;
    }

    _loaded = !_featureNames.empty() && rowCount > 0;
    if (_loaded) {
        INFO("[CacheFeatureNode:{}] loaded {} rows, {} columns", _id, rowCount, _featureNames.size());
    }
    return _loaded;
}

NodeProcessResult CacheFeatureNode::Process(const String& strategy, DataContext& context) {
    if (!_loaded) return NodeProcessResult::Error;

    size_t nBars = _data.empty() ? 0 : _data.begin()->second.size();
    if (_currentBar >= nBars) return NodeProcessResult::Finished;

    for (auto& key : _featureNames) {
        auto& values = _data[key];
        if (_currentBar < values.size()) {
            context.add(key, values[_currentBar]);
        }
    }
    ++_currentBar;
    return NodeProcessResult::Success;
}

Map<String, ArgType> CacheFeatureNode::out_elements() {
    Map<String, ArgType> result;
    for (auto& key : _featureNames) {
        result[key] = Double_Scalar;
    }
    return result;
}
