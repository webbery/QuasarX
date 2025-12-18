#pragma once
#include "std_header.h"
#include "Util/datetime.h"
#include "Util/system.h"
#include "nng/nng.h"
#include <memory>
#include <mutex>
#include <sys/types.h>
#include <thread>
#include "Bridge/exchange.h"
#include "json.hpp"

struct AgentStrategyInfo;
struct FeatureNode;
class IFeature;

class FeatureSubsystem {
    enum class FeatureKind {
        LongGap,
        SecondLevel,
        Collection,
    };

    struct PipelineInfo {
        char _gap:7 = 1;    // 间隔时长, 

        Map<size_t, IFeature*> _features;  // 预测特征，频率可能是天，也可能是分钟, 将用于发送到下一阶段来生成新的信号
        Map<size_t, IFeature*> _reals;     // 实时特征，频率是秒, 将用于发送到下一阶段来生成新的信号(比如预测X日操作，则使用该特征做择时)
        Map<size_t, IFeature*> _externals; // 额外捕获的特征，不发送到下一阶段，仅收集作为服务数据提供
        Map<size_t, String> _externalNames;
        Map<String, List<feature_t>> _collections; // 额外采集的特征
    };

public:
    FeatureSubsystem(Server* handle);
    ~FeatureSubsystem();

    void EraseConfig(const String& name);

    bool Start();
    bool Start(const String& name, bool is_simulate);

    void Stop();
    void Stop(const String& name);

    void AddPipeline(const String& name, const List<IFeature*>& feats, const Set<symbol_t>& symbs, bool is_sim);

    void ErasePipeline(const String& name);

    Set<symbol_t> GetFeatureSymbols();

    void InitSecondLvlFeatures();

    void RegistCollection(const String& strategy, const String& featureName, const nlohmann::json& params);

    void ClearCollections(const String& strategy);

    Map<symbol_t, Map<String, List<feature_t>>> GetCollection(const String& strategy);
    const Map<String, List<feature_t>>& GetCollection(symbol_t symbol) const ;

private:
    // extract tech index from quote/others
    void run();
    // 是否在开盘时间内
    bool is_open(symbol_t symbol, time_t);

    void send_feature(nng_socket& s, const QuoteInfo& quote, const Map<size_t, IFeature*>& pFeats);
    
    void CreateFeature(const String& strategy, const String& name, const nlohmann::json& params, FeatureKind);
    
    void UpdateExternalFeature(PipelineInfo& pipeinfo, const QuoteInfo& quote);
private:
    Server* _handle;

    Map<symbol_t, PipelineInfo> _pipelines;
    Map<String, Set<symbol_t>> _tasks;
    // 所有的特征
    Map<size_t, IFeature*> _features;
    
    std::thread* _thread;
    std::mutex _mtx;
    
};