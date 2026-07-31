#pragma once

// PCH wrapper: CMake target_precompile_headers 会同时为 C/C++ 生成 PCH，
// C 编译器不能包含 C++ 标准库头文件，用 __cplusplus 隔离。

#ifdef __cplusplus
#include <std_header.h>
#include <json.hpp>
#endif
