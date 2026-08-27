#pragma once

#include "StrategyNode.h"

class CacheFeatureNode : public QNode {
public:
    CacheFeatureNode() = default;
    ~CacheFeatureNode() override = default;

    bool Init(const nlohmann::json& config) override;
    NodeProcessResult Process(const String& strategy, DataContext& context) override;
    Map<String, ArgType> out_elements() override;

    const Set<symbol_t>& GetSymbols() const { return _symbols; }

private:
    String _cachePath;
    Vector<String> _featureNames;
    Map<String, Vector<double>> _data;
    Set<symbol_t> _symbols;
    size_t _currentBar = 0;
    bool _loaded = false;

    bool LoadCache();
};
