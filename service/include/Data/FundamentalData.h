#pragma once
#include "Data/DownloadBase.h"

/// 基本面数据（财务类目）
///
/// 下载：tools/fetch_finance_data.py --code <sym> --category <cat> --download
/// 体检：
///   CSV 级 —— 表头必须覆盖该类目声明的每一个列、statDate 必须是季末、
///             pubDate 必须存在且不早于 statDate、季度数够不够
///   DB  级 —— 表建出来没有、行数、最新报告期、每列非空率
///             （列名写错会表现为「整列 100% NULL」，这里能直接抓到）
/// 导入：FinanceDB::importCsv（内部已按 500 行分块，避开 DuckDB ART 索引损坏）
class FundamentalData : public DownloadBase {
public:
    FundamentalData(const String& financeDir, const String& interpreter);

    int  Download(const String& dst) override;
    bool Check() override;
    int  ImportData() override;
    int  UpdateData() override;

    DataType Type() const override { return DataType::Fundamental; }

    /// 要下载的标的（内部格式 sh.600519）
    void SetSymbols(const Vector<symbol_t>& symbols) { _symbols = symbols; }
    /// 要下载的类目，默认全部六个
    void SetCategories(const Vector<DataCategory>& cats) { _categories = cats; }
    /// 下载窗口，格式 "2026" 或 "2026-Q3"，空 = 不限
    void SetWindow(const String& start, const String& end) {
        _startYq = start;
        _endYq = end;
    }
    /// 每个类目至少要有几个季度（历史深度不够撑不起 lookback）
    void SetMinQuarters(int n) { _minQuarters = n; }

    /// 全部财务类目
    static Vector<DataCategory> AllCategories();

    /// 类目 → FinanceDB 的表名（也是脚本的 --category 取值）
    static const char* CategoryKey(DataCategory category);

    /// 表名 → 类目（找不到返回 false）
    static bool CategoryFromKey(const String& key, DataCategory& out);

private:
    /// 下载单个 (标的, 类目)，成功后 outCsv 指向 CSV 路径
    bool downloadOne(symbol_t symbol, DataCategory category, String& outCsv);
    /// CSV 级体检：不合格返回 false 并记异常
    bool checkCsv(symbol_t symbol, DataCategory category, const String& path);
    /// DB 级复查
    void checkDb();
    /// 对单个 (标的, 类目) 走一遍 下载→体检→导入
    int refreshOne(symbol_t symbol, DataCategory category);

    String _financeDir;
    String _interpreter;
    Vector<symbol_t> _symbols;
    Vector<DataCategory> _categories;
    String _startYq;
    String _endYq;
    int _minQuarters = 4;

    /// 本轮下载到的 CSV：类目 → (标的 → CSV 路径)
    Map<DataCategory, Map<symbol_t, String>> _downloaded;
    /// CSV 级体检没过的 (类目, 标的)
    Set<std::pair<DataCategory, symbol_t>> _bad;
};
