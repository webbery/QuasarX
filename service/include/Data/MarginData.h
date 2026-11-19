#pragma once
#include "Data/DownloadBase.h"

/// 两融明细（融资融券，逐交易日全市场）
///
/// 下载：tools/fetch_margin_data.py --out-dir <绝对路径>（每个交易所一个 CSV）
/// 体检：
///   CSV 级 —— 表头列完整性、公告日必须等于交易日 + 1 天、statDate 必须是
///             目标交易日、每个交易所的行数下限、两个交易所是否都拿到了
///   DB  级 —— 最新交易日、标的数、公告日越界行数、每列非空率
/// 导入：FinanceDB::importCsv（内部按 500 行分块）
class MarginData : public DownloadBase {
public:
    MarginData(const String& financeDir, const String& interpreter);

    int  Download(const String& dst) override;
    bool Check() override;
    int  ImportData() override;
    int  UpdateData() override;

    DataType Type() const override { return DataType::Margin; }

    /// 目标交易日（空 = 最近一个已收盘的交易日）
    void SetTargetDay(const String& day) { _targetDay = day; }
    /// 每个交易所至少要有多少行（低于此值认为这次抓取不完整）
    void SetMinRowsPerExchange(int n) { _minRows = n; }
    /// 期望的交易所后缀（默认 sh + sz）
    void SetExchangeSuffixes(const Vector<String>& sfx) { _exchanges = sfx; }

private:
    /// CSV 级体检：不合格返回 false 并记异常
    bool checkCsv(const String& path, const String& exchange);
    /// DB 级复查
    void checkDb();
    /// 从 CSV 文件名里认出交易所后缀（detail_20260113.sh.csv → sh）
    static String exchangeOf(const String& path);

    String _financeDir;
    String _interpreter;
    String _targetDay;
    int _minRows = 1000;
    Vector<String> _exchanges;

    /// 本轮下载到的 CSV：交易所后缀 → CSV 路径
    Map<String, String> _downloaded;
    /// 体检没过的交易所
    Set<String> _bad;
};
