#pragma once
#include "StrategyNode.h"
#include "Util/system.h"
#include <filesystem>

class Server;

/**
 * @brief 宏观数据输入节点
 *
 * 从 FRED/akshare 缓存文件读取宏观经济指标数据，对齐到日频时间轴。
 *
 * 时间对齐策略：
 *   - 中国数据：前向填充（forward fill）
 *   - 美国数据：T-1 滞后（数据发布延迟，使用前一天的值）
 *
 * 数据来源：
 *   - 缓存文件：{db_path}/macro/{country}/{indicator}.json
 *   - 格式：{"data": [{"date": "2024-01-02", "value": 5.32}, ...]}
 */
class MacroInputNode : public QNode {
public:
    RegistClassName(MacroInputNode);
    static const nlohmann::json getParams();

    MacroInputNode(Server* server);

    bool Init(const nlohmann::json& config) override;
    void Prepare(const String& strategy, DataContext& context) override;
    NodeProcessResult Process(const String& strategy, DataContext& context) override;

    Map<String, ArgType> out_elements() override;

private:
    /**
     * @brief 从缓存文件加载宏观数据
     */
    bool loadMacroData();

    /**
     * @brief 前向填充：返回目标日期及之前最近的值
     */
    double forwardFill(const String& targetDate) const;

    /**
     * @brief T-1 滞后：返回目标日期前一天的值
     */
    double lagT1(const String& targetDate) const;

    /**
     * @brief 线性插值
     */
    double linearInterp(const String& targetDate) const;

private:
    Server* _server;
    String _country;
    String _indicator;
    String _seriesId;
    String _fillMethod;

    // 缓存的宏观数据（日期 -> 值），按日期排序
    Vector<std::pair<String, double>> _macroData;

    // 当前处理位置（用于优化查找）
    size_t _currentIndex = 0;
};
