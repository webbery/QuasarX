#pragma once
#include "StrategyNode.h"
#include "std_header.h"

class DebugNode: public QNode {
public:
    RegistClassName(DebugNode);
    DebugNode(Server* server);

    static const nlohmann::json getParams();

    virtual bool Init(const nlohmann::json& config);

    virtual void Prepare(const String& strategy, DataContext& context) override;

    virtual NodeProcessResult Process(const String& strategy, DataContext& context) override;

    virtual void Done(const String& strategy);

    virtual Map<String, ArgType> out_elements() override;

private:
    /**
     * @brief 导入模式：读取 CSV 并注入到 context
     * @param context 数据上下文
     * @return true 成功，false 失败
     *
     * CSV 格式: datetime,symbol1.feature1,symbol1.feature2,...
     *   - datetime 列: "%Y-%m-%d %H:%M:%S" 格式
     *   - 其余列: "{symbol}.{feature}" 格式，例如 "sz.000423.m_minus"
     *   - 每行一个时间点，所有数据列共享同一时间轴
     */
    bool importFromCsv(DataContext& context);

    Server* _server;
    String _suffix;
    String _label;
    String _mode = "export";       // "export" (default) | "import"
    String _filePath;              // import 模式下的 CSV 文件路径
    Set<String> _inNames;
    Map<String, ArgType> _outElements;  // import 模式: 列名 → Double_TimeSeries
    DataContext* _context;
};
