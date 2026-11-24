#pragma once
#include "std_header.h"
#include "Util/system.h"   // symbol_t 的 get_symbol / null_symbol / is_etf
#include "json.hpp"
#include <functional>
#include <mutex>

/// 数据类型
enum class DataType : char {
    Quote,        ///< 行情
    Margin,       ///< 两融
    Fundamental,  ///< 基本面
};

/// 数据子类别：行情=频率，两融=表，基本面=财务类目
enum class DataCategory : char {
    Day,
    Min5,
    Min15,
    Min30,
    Min60,
    MarginDaily,
    Profit,
    Operation,
    Growth,
    Balance,
    Cashflow,
    Dupont,
};

/// 异常原因
enum class DataReason : char {
    Missing,       ///< 没有数据
    Stale,         ///< 数据停在旧日期，没跟上
    Invalid,       ///< 数据本身不对
    Insufficient,  ///< 有数据但不够（历史深度不足）
    Mismatch,      ///< 列名/结构对不上数据源
};

/// 一条数据异常
///
/// 只要「哪类数据 + 哪个标的 + 哪天 + 什么原因」就能定位问题并决定怎么补：
///   _type + _category → 用哪个脚本补（ToolFor）
///   _symbol           → 补哪个标的（null_symbol() 表示全市场级数据）
///   _date             → 数据断在哪天 / 缺哪天
struct DataIssue {
    DataType     _type = DataType::Quote;
    DataCategory _category = DataCategory::Day;
    symbol_t     _symbol = null_symbol();
    String       _date;    ///< 异常日期 YYYY-MM-DD
    DataReason   _reason = DataReason::Missing;
};

/// 下载基类
///
/// 把「下载 → 体检 → 导入 → 补数据」四步的公共骨架抽到这里，子类只填
/// 数据源与库表相关的差异：
///
///   Download(dst)   跑 python 脚本把数据落到 _dst
///   Check()         对落盘的 CSV 做体检（CSV 级）
///   ImportData()    入库，并在末尾做一次 DB 级复查
///   UpdateData()    针对体检出的问题重新下载并入库
///
/// 基类提供三组公共能力：
///   脚本   —— 统一跑 tools/ 下的脚本、统一解析 manifest
///             （此前 server.cpp / data.cpp / FinanceHandler.cpp 各写了一份，
///               第三份的哨兵格式还不一样）
///   CSV    —— 统一 BOM / 引号 / \r / 表头定位
///   异常   —— 统一记录与取走（凌晨补数据时取用）
class DownloadBase {
public:
    virtual ~DownloadBase() {}

    // 下载数据到指定位置
    virtual int Download(const String& dst) = 0;
    // 对下载的数据做检查
    virtual bool Check() = 0;
    // 导入数据到duckdb中
    virtual int ImportData() = 0;
    // 更新修复有问题的数据
    virtual int UpdateData() = 0;

    // ── 通用能力（子类共用）──

    /// 数据类型
    virtual DataType Type() const = 0;

    /// 本次体检出的异常清单
    const Vector<DataIssue>& Issues() const { return _issues; }

    /// 清空本轮异常清单（由编排在每轮开始时调用）
    void ClearIssues() { _issues.clear(); }

    /// 追加一条异常到本次体检结果
    void AddIssue(DataCategory category, symbol_t symbol, const String& date,
                  DataReason reason);

    /// 下载根目录
    const String& Dst() const { return _dst; }

    /// 直接指定下载根目录（补数据时还没跑过 Download，需要先告诉它落哪）
    void SetDst(const String& dst) { _dst = dst; }

    /// 这类数据补数据该用哪个下载脚本
    static const char* ToolFor(DataType type);

    /// 本轮累计导入行数（入库阶段 + 补数据重试阶段）
    int RowsImported() const { return _rowsImported; }

    /// 清零（每轮链路开始时调用）
    void ResetRows() { _rowsImported = 0; }

    /// 单项进度回调：下载阶段每处理完一个标的调一次。
    ///
    /// 单个标的的 python 下载几十秒、大批量几分钟，阶段之间不会有任何事件。
    /// 没有它，客户端只能看着一张不动的卡分不清「还在跑」和「已经死了」。
    using ItemCallback = std::function<void(const String& symbol, int done, int total)>;

    /// 设置单项进度回调（不设就不上报）
    void SetItemCallback(ItemCallback cb) { _itemCb = std::move(cb); }

public:
    /// 记录到全局队列（凌晨补数据时取用）
    static void RecordIssues(const Vector<DataIssue>& issues);
protected:
    /// 记一次导入行数。子类在 importCsv 之后调用 ——
    /// 入库和补数据两段都要记，否则终态事件只报第一段的行数
    void AddRows(int n) { if (n > 0) _rowsImported += n; }

    int _rowsImported = 0;

    // ── 脚本 ──

    /// 跑 tools/ 下的一个 python 脚本。
    /// 脚本的工作目录固定是 tools 目录（与既有实现一致：cd ../tools && ...）。
    /// @param interpreter python 解释器（由 PythonEnv 按配置解析）
    /// @param script      脚本文件名，如 "download_quote_bs.py"
    /// @param args        脚本参数（已自行处理引号）
    /// @param output      输出（stdout + stderr 合并，脚本的诊断信息也在这里）
    /// @return            命令是否执行成功（不代表脚本业务成功）
    static bool RunScript(const String& interpreter, const String& script,
                          const String& args, String& output);

    /// 从脚本输出里抠出 JSON manifest。
    /// 脚本的诊断行与 JSON 共用一条管道，所以取第一个 '{' 到最后一个 '}'。
    static bool ParseManifest(const String& output, nlohmann::json& out);

    /// manifest 里声明的产物文件。
    /// 兼容两种形态：字符串数组（fetch_margin_data.py）与对象数组
    /// （fetch_finance_data.py，取对象的 path 字段）。
    static Vector<String> ManifestFiles(const nlohmann::json& manifest);

    /// 给参数加单引号（路径里有空格时用）
    static String ShellQuote(const String& s);

    // ── CSV ──

    /// 打开 CSV 读表头（去 BOM、去 \r），返回列名。文件打不开返回 false。
    static bool ReadCsvHeader(const String& path, Vector<String>& cols);

    /// 切分一行（支持双引号包裹的字段）
    static Vector<String> SplitCsvLine(const String& line);

    /// 找列下标，找不到返回 -1
    static int ColIndex(const Vector<String>& cols, const String& name);

    /// 表头里缺了哪些期望列
    static Vector<String> MissingColumns(const Vector<String>& cols,
                                         const Vector<String>& expected);

    /// 取走全局队列里的异常（取完清空）
    static Vector<DataIssue> TakeIssues();

    /// 从全局队列里取回本类型的异常（其它类型的原样放回队列）。
    /// 三条数据链路可能并发跑，共用一个队列，所以不能让一条链路把别人的问题拿走。
    static Vector<DataIssue> TakeMyIssues(DataType type);

    String _dst;                 ///< 下载根目录
    Vector<DataIssue> _issues;   ///< 本次体检结果
    ItemCallback _itemCb;        ///< 单项进度回调
};
