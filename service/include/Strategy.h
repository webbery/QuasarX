#pragma once
#include "std_header.h"
#include "json.hpp"

#define BASIC_NAME  "Basic"

enum class ContractOperator: unsigned char {
  Hold = 0,
  Buy = 1,
  Sell = 2,
  Short = 4,
  Done = (0x1<<7),
};

template <>
struct fmt::formatter<ContractOperator> {
    constexpr auto parse(format_parse_context& ctx) {
        return ctx.begin(); // �����ֱ�ӷ���
    }

    template <typename FormatContext>
    auto format(ContractOperator op, FormatContext& ctx) const {
        String info;
        switch (op)
        {
        case ContractOperator::Hold: info = "Hold"; break;
        case ContractOperator::Buy: info = "Buy"; break;
        case ContractOperator::Sell: info = "Sell"; break;
        case ContractOperator::Short: info = "Short"; break;
        case ContractOperator::Done: info = "Done"; break;
        default: break;
        }
        return fmt::format_to(ctx.out(), "{}", info);
    }
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
     * @brief ���������������������ش���������
     */
    virtual feature_t Process(const feature_t& input) = 0;
    /**
    * @brief �󶨵�����ڵ���
     */
    virtual void Bind(const List<String>& names) {}

    void Update(const nlohmann::json& args) {
        _params = args;
    }

    const nlohmann::json& getParams() { return _params; }
    
    String name() const { return _name; }
    void setName(const String& name){ _name = name; }
protected:
    String _name;
    nlohmann::json _params;
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
