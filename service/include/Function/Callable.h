#pragma once
#include "DataContext.h"
#include "Util/data.h"

/**
 * @brief 缺失值处理方式
 */
enum class LostType {
    Fill,           // 填充（使用 FillMethod 指定的策略）
    Delete,         // 删除
    Interplot,      // 插值: K临近插补;MICE插补
};

class ICallable {
public:
    virtual ~ICallable(){}
    virtual context_t operator()(const Map<String, context_t>& args) = 0;
};
