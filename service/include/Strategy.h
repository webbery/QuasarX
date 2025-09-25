#pragma once
#include "std_header.h"
#include "json.hpp"

#define BASIC_NAME  "Basic"

enum class ContractOperator: unsigned char {
  Hold = 0,
  Long = 1,
  Sell = 2,
  Short = 4,
  Done = (0x1<<7),
};

enum class StrategyType: char {
    ST_Unknow,
    ST_InterDay,
    ST_IntraDay,
    ST_Count,
};

enum class PredictType: char {
    PT_UpDown,
    
};
class IStrategy {
public:
    virtual ~IStrategy() {}
    virtual int generate(const Vector<float>& prediction) = 0;
    virtual bool is_valid() = 0;
};

class QNode {
public:
    virtual ~QNode(){}
    /**
     * @brief 对输入数据做处理，并返回处理后的数据
     */
    virtual feature_t Process(const feature_t& input) = 0;
    /**
    * @brief 绑定的输入节点名
     */
    virtual void Bind(const List<String>& names) {}

    String name() const { return _name; }
    void setName(const String& name){ _name = name; }
protected:
    String _name;
};

class QFeature : public QNode {
public:
    virtual feature_t Process(const feature_t& input);
};

class QAgent: public QNode {
public:
    virtual feature_t Process(const feature_t& input);
};

class QStrategy: public QNode {
public:
    QStrategy();

    virtual feature_t Process(const feature_t& input);

    void setT0(bool yes) { _isT0 = yes; }
    bool isT0() { return _isT0; }

protected:
    bool _isT0;
};

struct AgentStrategyInfo;
AgentStrategyInfo parse_strategy_script(const nlohmann::json& content);
