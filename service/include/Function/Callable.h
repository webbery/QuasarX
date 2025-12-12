#pragma once
#include "std_header.h"

/**
 * @brief 缺失值处理方式
 */
enum class LostType {
    Fill,           // 填充
    Delete,         // 删除
    Interplot,      // 插值: K临近插补;MICE插补
};
/**
 * @brief 数据填充方式
 */
enum class FillType {
    Const,          // 以指定值填充
    Prev,
    After,
};

class ICallable {
public:
    virtual ~ICallable(){}
    virtual feature_t operator()(const Map<String, feature_t>& args) = 0;
};
