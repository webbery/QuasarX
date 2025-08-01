#pragma once
#include "Util/datetime.h"
#include "std_header.h"
#include "Util/system.h"
#include "json.hpp"
#include "nng/nng.h"
#include <memory>
#include <mutex>
#include <sys/types.h>
#include <thread>
#include "Bridge/exchange.h"
#include "Feature.h"

struct AgentStrategyInfo;

struct FeatureNode;
class ATRFeature: public PrimitiveFeature {
public:
    ATRFeature(const nlohmann::json& params);
    ~ATRFeature();
    virtual bool plug(Server* handle, const String& account);

    virtual double deal(const QuoteInfo& quote, double extra = 0);

    virtual const char* desc();

    virtual FeatureType type() { return FeatureType::FT_ATR; }

    virtual size_t id();

    static constexpr StringView name();

private:
    double tr(short index);
private:
    short _T = 10;
    unsigned short _cur = 0;
    unsigned short _cnt = 0;
    double* _close = nullptr;
    Vector<double> _tr;
    double _sum;
};


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

private:
    Server* _handle;

    

    struct PipelineInfo {
        char _gap:7 = 1;    // 间隔时长, 
        List<FeatureBlock*> _features;  // 预测特征
        List<IFeature*> _reals;     // 实时特征
    };

    Map<symbol_t, PipelineInfo> _pipelines;
    Map<String, Set<symbol_t>> _tasks;
    List<IFeature*> _features;
    
    std::thread* _thread;
    std::mutex _mtx;
    
};