#pragma once
#include "std_header.h"
#include "Data/DownloadBase.h"
#include <mutex>

/// 刷新状态（持久化到 {dbPath}/data_refresh_state.json）
///
/// 记录每条数据链路最后成功刷新的「目标交易日」。
///
/// 调度不再用「今天几点跑一次」这种一次性触发 —— 那种写法在 20:00 那一分钟
/// 内重启就会丢掉一整天，而且进程晚上 23:00 才起来当天也不会补。
/// 改成判断「目标交易日是否已经刷过」之后：昨天没跑成功、进程晚启动、
/// 20:00 那分钟重启过，都会自动补上。
///
/// 由 Server 持有一个实例（进程内只有一份状态），不要做成全静态 ——
/// 那样就没有生命周期可言，测试也没法隔离。
class RefreshState {
public:
    /// dbPath 是数据库根目录，状态文件放在它下面
    explicit RefreshState(const String& dbPath);

    /// 某条链路最后刷新的目标交易日（没有记录则空串）
    String LastDone(const String& key) const;

    /// 标记某条链路已刷到某个目标交易日
    void MarkDone(const String& key, const String& targetDay);

    /// 某条链路是否还需要刷新（目标交易日 != 已刷新的）
    bool NeedRefresh(const String& key, const String& targetDay) const;

    /// 清掉某条链路的记录（下次无条件刷新）
    void Clear(const String& key);

private:
    void load();
    void save() const;

    String              _path;
    mutable std::mutex  _mtx;
    Map<String, String> _states;
};

/// 下载 / 体检 / 导入 编排
///
/// 一次 Run 的顺序：
///   Download(dst)   数据落到本地
///   Check()         CSV 级体检，问题记进异常清单
///   ImportData()    好的数据入库（坏的跳过），末尾做 DB 级复查
///   UpdateData()    针对清单里的问题立刻重下重试一次
///
/// 异常清单是全局的：Run 结束后本轮的问题进队列，UpdateData 从队列里取回
/// 本类型的部分重试，仍失败的放回队列。凌晨 03:00 的 Repair() 走同一条路径，
/// 补的是上一轮遗留的问题。
/// 下载链路的阶段（进度上报用）
///
/// 用枚举而不是字符串：阶段集合是封闭的（下载/体检/入库/重试），
/// 字符串一旦拼错，只会在前端静默匹配不上、进度条永远停在第一格；
/// 枚举在编译期就能挡住。
enum class DataStage : uint8_t {
    Download,   ///< 拉取数据（阻塞式 Python 调用，最耗时）
    Check,      ///< CSV 级体检
    Import,     ///< 入库（末尾含 DB 级复查）
    Retry,      ///< 对体检发现的问题立刻重下重试
};

class DataPipeline {
public:
    /// 阶段回调：进入每个阶段时调用一次。
    ///
    /// 只报阶段、不报条目数 —— 下载是 `_data.Download()` 里的一次阻塞式 Python 调用，
    /// 要拿到逐标的进度得先把 `RunCommand` 改成流式读子进程 stdout。
    /// 在此之前，进度条能如实反映的就是「走到第几个阶段」。
    using StageCallback = std::function<void(DataStage stage)>;

    explicit DataPipeline(DownloadBase& data) : _data(data) {}

    /// 跑完整条链路。返回导入行数（<0 失败，0 无数据）
    /// onStage 可为空（不需要进度上报的调用方，如凌晨补数据）。
    int Run(const String& dst, const StageCallback& onStage = nullptr);

    /// 只跑补数据：把队列里本类型的遗留问题重下重试。
    /// dst 是这条链路的数据根目录（补数据时还没跑过 Download，得先告诉它落哪）
    int Repair(const String& dst);

    /// 本轮体检出的异常
    const Vector<DataIssue>& issues() const { return _data.Issues(); }

    /// 数据类型
    DataType type() const { return _data.Type(); }

    /// 本次刷新的目标交易日（最近一个已收盘的交易日）
    static String TargetTradingDay();

private:
    DownloadBase& _data;
};
