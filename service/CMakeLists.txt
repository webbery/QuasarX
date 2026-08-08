cmake_minimum_required(VERSION 3.20)
project(QuantService)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_NODE_IO_LOGGING "Enable node IO logging to DuckDB (debug only)" OFF)
if(ENABLE_NODE_IO_LOGGING)
    add_compile_definitions(ENABLE_NODE_IO_LOGGING)
    message(STATUS "Node IO logging: ENABLED")
else()
    message(STATUS "Node IO logging: DISABLED (zero overhead)")
endif()

set(CMAKE_CXX_STANDARD 23)
# add_compile_definitions(USE_PMR)

# OpenMP（CEEMDAN ensemble 并行化）
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
    message(STATUS "OpenMP found: ${OpenMP_CXX_VERSION}")
else()
    message(STATUS "OpenMP not found — CEEMDAN will run single-threaded")
endif()

# ── SIMD 指令集自动检测 ──
# 检测编译器对 AVX-512 / AVX2+FMA / AVX2 的支持，设置编译标志和宏定义
include(CheckCXXCompilerFlag)

set(SIMD_LEVEL "none")

if(MSVC)
    # MSVC: /arch:AVX512 定义 __AVX512F__; /arch:AVX2 定义 __AVX2__
    # 注意：SIMD 宏和编译标志都必须 per-file，不能全局设置
    # 全局 __AVX512F__/__AVX2__ 会导致 Eigen/DataFrame 等库在非 SIMD 文件中生成 AVX 代码
    # 而没有对应的 /arch 标志 → 寄存器状态不保存 → 崩溃
    #
    # 重要：check_cxx_compiler_flag 只检测编译器是否接受标志，不检测 CPU 是否支持
    # MSVC 的 /arch:AVX512 在任何编译器版本都接受，但 CPU 可能不支持（如 Ryzen 5000）
    # 因此 MSVC 默认最高到 AVX2，AVX-512 需要用户显式开启：
    #   cmake .. -DENABLE_AVX512=ON  （仅在有 AVX-512 的 CPU 上使用）
    check_cxx_compiler_flag("/arch:AVX2" COMPILER_SUPPORTS_AVX2)

    option(ENABLE_AVX512 "Enable AVX-512 (requires CPU support: Intel Xeon/Ice Lake+)" OFF)

    if(ENABLE_AVX512)
        check_cxx_compiler_flag("/arch:AVX512" COMPILER_SUPPORTS_AVX512)
        if(COMPILER_SUPPORTS_AVX512)
            set(SIMD_COMPILE_FLAG "/arch:AVX512")
            set(SIMD_DEFINES "__AVX512F__;__AVX2__;__SSE2__;__FMA__")
            set(SIMD_LEVEL "AVX-512 (512-bit, 8-wide double)")
            message(STATUS "SIMD: AVX-512 enabled (per-file, user opted in)")
        else()
            set(SIMD_COMPILE_FLAG "/arch:AVX2")
            set(SIMD_DEFINES "__AVX2__;__SSE2__;__FMA__")
            set(SIMD_LEVEL "AVX2 (256-bit, 4-wide double) [AVX-512 requested but compiler unsupported]")
            message(STATUS "SIMD: AVX2 fallback (AVX-512 not supported by compiler)")
        endif()
    elseif(COMPILER_SUPPORTS_AVX2)
        set(SIMD_COMPILE_FLAG "/arch:AVX2")
        set(SIMD_DEFINES "__AVX2__;__SSE2__;__FMA__")
        set(SIMD_LEVEL "AVX2 (256-bit, 4-wide double)")
        message(STATUS "SIMD: AVX2 detected (per-file)")
    else()
        set(SIMD_COMPILE_FLAG "")
        set(SIMD_DEFINES "__SSE2__")
        set(SIMD_LEVEL "SSE2 (128-bit, 2-wide double)")
        message(STATUS "SIMD: SSE2 only")
    endif()
else()
    # GCC/Clang: 逐级检测 -mavx512f / -mavx2 / -mfma
    check_cxx_compiler_flag("-mavx512f" COMPILER_SUPPORTS_AVX512)
    check_cxx_compiler_flag("-mavx2"   COMPILER_SUPPORTS_AVX2)
    check_cxx_compiler_flag("-mfma"    COMPILER_SUPPORTS_FMA)

    if(COMPILER_SUPPORTS_AVX512)
        set(SIMD_COMPILE_FLAG "-mavx512f;-mavx512dq;-mavx512bw")
        set(SIMD_DEFINES "__AVX512F__;__AVX2__;__SSE2__")
        set(SIMD_LEVEL "AVX-512 (512-bit, 8-wide double)")
        message(STATUS "SIMD: AVX-512 detected (per-file)")
        if(COMPILER_SUPPORTS_FMA)
            list(APPEND SIMD_COMPILE_FLAG "-mfma")
            set(SIMD_DEFINES "${SIMD_DEFINES};__FMA__")
        endif()
    elseif(COMPILER_SUPPORTS_AVX2)
        # AVX2：不定义 __AVX2__ 宏（避免非 AVX2 CPU 的 CI 机器 SIGILL）
        # SIMD 函数通过 __attribute__((target("avx2,fma"))) 编译出 AVX2 代码，
        # 但 #if defined(__AVX2__) 的代码路径不会被激活 → 回退 SSE2
        # 这是最安全的方案：二进制在任何 x86-64 CPU 上都能运行
        set(SIMD_COMPILE_FLAG "")
        set(SIMD_DEFINES "__SSE2__")
        set(SIMD_LEVEL "SSE2 (AVX2 target attr available but not activated)")
        message(STATUS "SIMD: SSE2 active (AVX2 per-function attr compiled but dormant)")
        if(COMPILER_SUPPORTS_FMA)
            message(STATUS "SIMD: FMA per-function target attr available")
        endif()
    else()
        set(SIMD_COMPILE_FLAG "")
        set(SIMD_DEFINES "__SSE2__")
        set(SIMD_LEVEL "SSE2 (128-bit, 2-wide double)")
        message(STATUS "SIMD: SSE2 only")
    endif()
endif()

message(STATUS "SIMD level: ${SIMD_LEVEL}")
execute_process(
  COMMAND git describe --tags --always --dirty
  TIMEOUT 15
  OUTPUT_VARIABLE GIT_TAG_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "Building from git tag: ${GIT_TAG_VERSION}")

# include(FetchContent)
if (WIN32)
add_compile_options(/utf-8 /Z7 /GF /bigobj /Zc:__cplusplus /DNOMINMAX /DJWT_DISABLE_PICOJSON /MP)
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    add_compile_options(/O2 /fp:fast /GL)
    add_link_options(/LTCG)
endif()
set(OpenSSL_ROOT_DIR "/usr/local/openssl" CACHE PATH "/usr/local/openssl") 
# find_package(OpenSSL REQUIRED )
set(OPENSSL_INCLUDE_DIR "${OpenSSL_ROOT_DIR}/include")
set(OpenSSL_LIBRARIES_DIR "${OpenSSL_ROOT_DIR}/lib")
set(LIB_EAY_DEBUG "${OpenSSL_LIBRARIES_DIR}/libcrypto.lib")
set(SSL_EAY_DEBUG "${OpenSSL_LIBRARIES_DIR}/libssl.lib")
set(OpenSSL_LIBRARIES ${LIB_EAY_DEBUG} ${SSL_EAY_DEBUG})
include_directories(${OPENSSL_INCLUDE_DIR})
add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT CPPHTTPLIB_DEBUGLOG)
else(WIN32)
# FetchContent_Declare(
#     cpr
#     GIT_REPOSITORY https://github.com/libcpr/cpr.git
#     GIT_TAG        1.10.5  # 指定版本号
# )
# FetchContent_MakeAvailable(cpr)

add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)
# 启用最高警告级别
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-reorder)
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
add_compile_definitions(_DEBUG)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
add_compile_options(-g -O0)
elseif("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
add_compile_options(-march=native -O3)
# -g1 最小调试信息（函数名+行号），供 .debug 符号文件 addr2line 崩溃栈解析；
# 相比 -g 省去类型/局部变量 DWARF，显著降低链接时间
add_compile_options(-g1 -fno-omit-frame-pointer)
add_compile_options(-Wl,-rpath=.)
endif()
add_compile_definitions(JWT_DISABLE_PICOJSON)

set(OPENSSL_ROOT_DIR "/usr/local/openssl") 
set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include")
set(OPENSSL_LIBRARY_DIR "${OPENSSL_ROOT_DIR}/lib64")
include_directories(${OPENSSL_INCLUDE_DIR})
if (EXISTS ${OPENSSL_LIBRARY_DIR}/libcrypto.a)
set(OPENSSL_CRYPTO_LIBRARY "${OPENSSL_LIBRARY_DIR}/libcrypto.a")
set(OPENSSL_SSL_LIBRARY "${OPENSSL_LIBRARY_DIR}/libssl.a")
set(OpenSSL_LIBRARIES ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY} )
endif()
endif(WIN32)

#find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
#message("-- Python: " ${Python3_EXECUTABLE})
#message("-- Python Lib: " ${Python3_LIBRARIES})
#add_compile_definitions(PYTHON_PATH=\"${Python3_EXECUTABLE}\")

option(USE_CUDA OFF)
option(USE_LIBTORCH OFF)

if (USE_LIBTORCH)
    add_compile_definitions(_USE_LIBTORCH_)
    find_package(Torch REQUIRED)
    message("-- libTorch Include Dir: ${TORCH_INCLUDE_DIRS}")
    message("-- libTorch Libs: ${TORCH_LIBRARIES}")
endif (USE_LIBTORCH)

if (USE_CUDA)
    add_compile_definitions(__USE_CUDA__)
    find_package(CUDA REQUIRED)
    message("-- CUDA Include Dir:" ${CUDA_INCLUDE_DIRS})
    message("-- CUDA Libs: " ${CUDA_LIBRARIES})

    # find tensorRT
    set(TensorRT_DIR "/opt/TensorRT" CACHE PATH "Path to TensorRT installation")

    set(TensorRT_INCLUDE_DIRS "${TensorRT_DIR}/include")
    set(TensorRT_LIBRARIES_DIR "${TensorRT_DIR}/lib")
else (USE_CUDA)
    # find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime/core/session/onnxruntime_c_api.h
    #     PATHS /usr/local/include
    #     NO_DEFAULT_PATH
    # )

    # find_library(ONNXRUNTIME_LIBRARY onnxruntime
    #     PATHS /usr/local/lib
    #     NO_DEFAULT_PATH
    # )

    # if(ONNXRUNTIME_INCLUDE_DIR AND ONNXRUNTIME_LIBRARY)
    #     message("-- Found ONNX Runtime: ${ONNXRUNTIME_LIBRARY}")
    #     include_directories(${ONNXRUNTIME_INCLUDE_DIR})
    #     set(ONNXRUNTIME_LIBRARIES ${ONNXRUNTIME_LIBRARY})
    # else()
    #     find_package(onnxruntime REQUIRED PATHS /usr/local/lib/cmake/onnxruntime)
    #     set(onnxruntime_ROOT ${onnxruntime_DIR}/../../..)
    #     include_directories(${onnxruntime_ROOT}/include)
    #     link_directories(${onnxruntime_ROOT}/lib/)
    #     set(ONNXRUNTIME_LIBRARIES onnxruntime)
    #     # message(FATAL_ERROR "ONNX Runtime not found. Please install it first.")
    # endif()
endif (USE_CUDA)

include_directories(include third_party/include ${CUDA_INCLUDE_DIRS} ${TensorRT_INCLUDE_DIRS} ${TORCH_INCLUDE_DIRS})
if (WIN32)
if (USE_CUDA)
    find_library(NVINFER_LIB nvinfer_10 PATHS ${TensorRT_LIBRARIES_DIR})
    find_library(NVONNXPARSER_LIB nvonnxparser_10 PATHS ${TensorRT_LIBRARIES_DIR})
    set(TensorRT_LIBRARIES ${NVINFER_LIB} ${NVONNXPARSER_LIB})
else (USE_CUDA)
endif (USE_CUDA)

link_directories(${PROJECT_SOURCE_DIR}/third_party/libs/win)
else(WIN32)
if (USE_CUDA)
    find_library(NVINFER_LIB nvinfer PATHS ${TensorRT_LIBRARIES_DIR})
    find_library(NVONNXPARSER_LIB nvonnxparser PATHS ${TensorRT_LIBRARIES_DIR})
    set(TensorRT_LIBRARIES ${NVINFER_LIB} ${NVONNXPARSER_LIB})
else (USE_CUDA)
endif (USE_CUDA)

link_directories(${PROJECT_SOURCE_DIR}/third_party/libs/unix ${PROJECT_BINARY_DIR}/_deps/curl-build/lib)
endif(WIN32)

file(GLOB_RECURSE prj_files_header include/*.h include/*.hpp)
file(GLOB_RECURSE prj_files_cpp src/*.cpp src/*.c) 

source_group(TREE ${PROJECT_SOURCE_DIR}/include PREFIX "HeaderFiles" FILES ${prj_files_header})
source_group(TREE ${PROJECT_SOURCE_DIR}/src PREFIX "SourceFiles" FILES ${prj_files_cpp})

add_executable(${PROJECT_NAME} ${prj_files_cpp} ${prj_files_header})

# ── SIMD 编译标志仅应用于算法文件 ──
# 全局 /arch:AVX2 或 __AVX2__ 会导致第三方库（Eigen/DataFrame/fmt 等）
# 在非 SIMD 文件中生成 AVX 代码，但没有对应的 /arch 标志 → 寄存器状态不保存 → 崩溃
if(SIMD_COMPILE_FLAG)
    set(SIMD_SOURCE_FILES
        # ── EMD/CEEMDAN 算法 ──
        src/Algorithms/EMD_SIMD.cpp
        src/Algorithms/EMD.cpp
        src/Algorithms/CEEMDAN.cpp
        src/Nodes/EMDNode.cpp
        # ── Eigen 密集矩阵运算 ──
        src/Nodes/FunctionNode.cpp       # R2 (线性回归)
        src/Nodes/StackNode.cpp          # 矩阵堆叠
        src/Nodes/HMMNode.cpp            # HMM 前向-后向
        src/Function/Function.cpp        # 数值计算工具
        src/Metric/RiskMetric.cpp        # 风险指标（自相关、Sharpe 等）
        src/Metric/Covariance.cpp        # 协方差 + 特征分解
        src/Handler/CUSUMHandler.cpp     # CUSUM 相关性结构检测
        src/Handler/CointegrationHandler.cpp  # Johansen 协整检验
        src/Handler/PortfolioHandler.cpp # 组合优化
        src/Handler/PCAHandler.cpp       # PCA 特征分解
        src/Handler/VolatilityHandler.cpp # 波动率模型
        src/Util/finance.cpp            # 金融工具（LedoitWolf、风险平价等）
    )
    foreach(src ${SIMD_SOURCE_FILES})
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
            set_source_files_properties(${src} PROPERTIES
                COMPILE_OPTIONS "${SIMD_COMPILE_FLAG}"
                COMPILE_DEFINITIONS "${SIMD_DEFINES}")
        endif()
    endforeach()
    list(LENGTH SIMD_SOURCE_FILES _simd_count)
    message(STATUS "SIMD flags applied to ${_simd_count} files")
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE QS_VERSION="${GIT_TAG_VERSION}")


if (WIN32)
set(DEBUG_LINK_LIBS ws2_32 xmdapi xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se traderapi sptraderapi nngd fmtlog-static duckdb)
set(OPTIMIZED_LINK_LIBS ws2_32 xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se xfastmdapi fasttraderapi fastsptraderapi nng fmtlog duckdb)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
#if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
target_link_libraries(${PROJECT_NAME} ${DEBUG_LINK_LIBS} Crypt32.lib ${OpenSSL_LIBRARIES})
#else ()
#target_link_libraries(${PROJECT_NAME} ${OPTIMIZED_LINK_LIBS})
#endif()
else(WIN32)
if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
add_compile_options(-Wl,-rpath=.)
set(LINUX_LIBS xfastmdapi fasttraderapi fastsptraderapi duckdb)
else()
if(ENABLE_ASAN)
    # 添加编译和链接的 ASan 标志
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
    # 可选：如果使用 CMake 3.13+ 也可用 target_compile_options / target_link_options 针对特定目标
endif()
set(LINUX_LIBS xmdapi traderapi sptraderapi duckdb)
endif()
if (EXISTS "${OPENSSL_LIBRARY_DIR}/libssl.a")
    target_compile_options(${PROJECT_NAME} PRIVATE -fPIE)
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-pie")
    target_link_libraries(${PROJECT_NAME} ${OpenSSL_LIBRARIES} dl z)
else()
    target_link_libraries(${PROJECT_NAME} ssl crypto)
endif()
target_link_libraries(${PROJECT_NAME} curl -pthread nng papi
    xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se
    ${LINUX_LIBS}
)

endif(WIN32)

set(FMT_TEST OFF)
set(FMT_DOC OFF) 
add_subdirectory(third_party/fmt)
add_subdirectory(third_party/DataFrame)
target_link_libraries(${PROJECT_NAME} fmt::fmt DataFrame::DataFrame xgboost)
if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} OpenMP::OpenMP_CXX)
endif()
if (USE_CUDA)
    target_link_libraries(${PROJECT_NAME} ${CUDA_LIBRARIES} ${TensorRT_LIBRARIES})
endif (USE_CUDA)
if (USE_LIBTORCH)
    target_link_libraries(${PROJECT_NAME} ${TORCH_LIBRARIES})
endif (USE_LIBTORCH)

# ── 预编译头 (PCH) ──
# std_header.h 传递包含 Eigen(127), Boost(141), spdlog(36), DataFrame(28) 等重头文件
# json.hpp 被 127 个编译单元使用
# 预编译后所有 .cpp 共享，避免重复解析 ~370 个头文件
# pch.h 内部用 #ifdef __cplusplus 保护，C 文件跳过 C++ 头文件
target_precompile_headers(${PROJECT_NAME} PRIVATE
    <pch.h>
)

if (CMAKE_BUILD_TYPE STREQUAL "Release" AND NOT WIN32)
  find_program(OBJCOPY_EXE objcopy REQUIRED)
  add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
      COMMAND ${OBJCOPY_EXE} --only-keep-debug $<TARGET_FILE:${PROJECT_NAME}> $<TARGET_FILE:${PROJECT_NAME}>.debug
      COMMAND ${OBJCOPY_EXE} --strip-debug $<TARGET_FILE:${PROJECT_NAME}>
      COMMAND ${OBJCOPY_EXE} --add-gnu-debuglink=$<TARGET_FILE:${PROJECT_NAME}>.debug $<TARGET_FILE:${PROJECT_NAME}>
      COMMENT "Separating debug symbols for ${PROJECT_NAME}"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
  )
endif()

install(TARGETS ${PROJECT_NAME})
install(FILES
    ${CMAKE_SOURCE_DIR}/build/${PROJECT_NAME}
    ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libxtpquoteapi.so
    ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libxtptraderapi.so
    ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libthosttraderapi_se.so
    ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libthostmduserapi_se.so
    DESTINATION installs)
install(FILES
    ${CMAKE_SOURCE_DIR}/tools/run_task.py
    ${CMAKE_SOURCE_DIR}/tools/quote_index.py
    DESTINATION installs/tools
)