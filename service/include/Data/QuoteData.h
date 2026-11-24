#pragma once
#include "Data/DownloadBase.h"
#include "Data/DataPipeline.h"

/// 行情数据（股票 / ETF，单频率）
///
/// 一个实例负责「一个频率 × 一批标的」。调度按频率各跑一遍。
///
/// 下载：tools/download_quote_bs.py → {dst}/{asset}_org/{freq}/ 与 {asset}_hfq/{freq}/
/// 体检：
///   CSV 级 —— 表头必须逐列等于 datetime,open,close,high,low,volume,turnover
///             （QuoteDB 是按位置解析的，表头一变价格就整体错位）；
///             datetime 严格递增不重复；OHLC 自洽（停牌 bar：volume=0、
///             量额空串或四价全 0/全空，跳过检查）；org 与 hfq 的日期集合一致
///   DB  级 —— 每个标的的 bar 数 / 最新时间 / OHLC 非法行数；
///             复权价与「分红事件重算」逐 bar 比对（以前只比最后一根）。
///             增量下载时只比对本次重写过的区间，历史部分在当初导入的那轮已比对过
/// 导入：QuoteDB::importCsv（Appender + prepared UPDATE，沿用既有实现）
class QuoteData : public DownloadBase {
public:
    QuoteData(const String& interpreter, const String& freq);

    int  Download(const String& dst) override;
    bool Check() override;
    int  ImportData() override;
    int  UpdateData() override;

    DataType Type() const override { return DataType::Quote; }

    /// 要下载的标的（内部格式 sh.600519）
    void SetSymbols(const Vector<symbol_t>& symbols) { _symbols = symbols; }
    /// true = 全量覆盖（忽略库里的最新日期）
    void SetOverwrite(bool overwrite) { _overwrite = overwrite; }
    /// 增量下载的结束日（空 = 今天）
    void SetEndDate(const String& end) { _endDate = end; }

    /// CSV 期望表头（QuoteDB 按位置解析，顺序不能变）
    static Vector<String> ExpectedColumns();

    /// 频率 → 体检子类别
    static DataCategory CategoryOf(const String& freq);

    /// 下载去重键：频率 + 标的集合。
    /// 只按频率去重会把不相干的请求一起吞掉 —— 另一批标的的下载根本没执行，
    /// 而前端照样拿到一个 job_id，等不到任何事件。
    static String DownloadKey(const Vector<symbol_t>& symbols, const String& freq);

    /// 从请求里挑出真正要下的那批（派发时按标的粒度去重后得到）
    static Vector<symbol_t> Pick(const Vector<symbol_t>& requested,
                                  const Set<symbol_t>& dispatching);

    /// 跑完整条行情链路：下载 → 体检 → 入库 → 补数据。
    /// dst 是数据根目录；onProgress 汇报阶段与阶段内进度；
    /// failedSymbols 回填体检未过、没入库的标的（去重后）。
    ///
    /// 阶段切换与逐标的进度合成一个回调，调用方只接一条通道。
    using ProgressCallback =
        std::function<void(DataStage stage, const String& symbol, int done, int total)>;

    int Run(const String& dst, const ProgressCallback& onProgress = nullptr,
            Vector<String>* failedSymbols = nullptr);

private:
    struct SymbolFiles {
        String _org;
        String _hfq;
        String _from;   ///< 增量下载起点（库里原有最新日期），空 = 全量
    };

    /// 下载单个标的
    bool downloadOne(symbol_t symbol, const String& start);
    /// CSV 级体检：不合格返回 false 并记异常
    bool checkCsv(symbol_t symbol, const SymbolFiles& files);
    /// DB 级复查（增量时只复查本次重写区间）
    void checkDb();
    /// 复权比对：`from` 之后（空 = 全区间）的下载值 vs 分红重算值，
    /// 不一致的恢复成下载值。返回不一致的 bar 数
    int reconcileAdj(symbol_t symbol, const String& table, const String& from);

    String _interpreter;
    String _freq;
    String _endDate;
    bool   _overwrite = false;
    Vector<symbol_t> _symbols;

    /// 本轮下载到的 CSV
    Map<symbol_t, SymbolFiles> _downloaded;
    /// CSV 级体检没过的标的
    Set<symbol_t> _bad;
};
