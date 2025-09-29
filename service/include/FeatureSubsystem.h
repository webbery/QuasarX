#pragma once
#include "Util/datetime.h"
#include "std_header.h"
#include "Util/system.h"
#include "nng/nng.h"
#include <memory>
#include <mutex>
#include <sys/types.h>
#include <thread>
#include "Bridge/exchange.h"

struct AgentStrategyInfo;
struct FeatureNode;
class IFeature;

class FeatureSubsystem {
public:
    FeatureSubsystem(Server* handle);
    ~FeatureSubsystem();

    /**
     * @brief load feature configuration of given holding
     * 
     */
    void LoadConfig(const AgentStrategyInfo& config);

    void EraseConfig(const String& name);

    bool Start();
    bool Start(const String& name, bool is_simulate);

    void Stop();
    void Stop(const String& name);

    void AddPipeline(const String& name, const List<IFeature*>& feats, const Set<symbol_t>& symbs, bool is_sim);

    void ErasePipeline(const String& name);

    Set<symbol_t> GetFeatureSymbols();

    void InitSecondLvlFeatures();

    void RegistCollection(const String& strategy, const Set<String>& names);

    void ClearCollections(const String& strategy);

    Map<symbol_t, Map<String, std::variant<float, List<float>>>> GetCollection(const String& strategy);
    const Map<String, std::variant<float, List<float>>>& GetCollection(symbol_t symbol) const ;

private:
    struct FeatureBlock{
        IFeature* _feature;
        Set<FeatureBlock*> _nexts;
    };
    // extract tech index from quote/others
    void run();
    // 是否在开盘时间内
    bool is_open(symbol_t symbol, time_t);

    void send_feature(nng_socket& s, const QuoteInfo& quote, List<IFeature*>* pFeats);
    void send_feature(nng_socket& s, const QuoteInfo& quote, const List<FeatureBlock*>& pFeats);

    FeatureBlock* GenerateBlock(FeatureNode* node);

    double recursive_feature(FeatureBlock* block, const QuoteInfo& quote, double cur);

    void DeleteBlock(FeatureBlock* block);
    
private:
    Server* _handle;

    struct PipelineInfo {
        char _gap:7 = 1;    // 间隔时长, 
        List<FeatureBlock*> _features;  // 预测特征
        List<IFeature*> _reals;     // 实时特征
        Map<String, std::variant<float, List<float>>> _collections; // 额外采集的特征
    };

    Map<symbol_t, PipelineInfo> _pipelines;
    Map<String, Set<symbol_t>> _tasks;
    List<IFeature*> _features;
    
    std::thread* _thread;
    std::mutex _mtx;
    
};