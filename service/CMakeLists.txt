cmake_minimum_required(VERSION 3.20)
project(QuantService)

set(CMAKE_CXX_STANDARD 23)
# add_compile_definitions(USE_PMR)

# include(FetchContent)
if (WIN32)
add_compile_options(/utf-8 /Z7 /GF /Zc:__cplusplus /DNOMINMAX)
else(WIN32)
# FetchContent_Declare(
#     cpr
#     GIT_REPOSITORY https://github.com/libcpr/cpr.git
#     GIT_TAG        1.10.5  # 指定版本号
# )
# FetchContent_MakeAvailable(cpr)

if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
add_compile_definitions(_DEBUG)
endif()
endif(WIN32)

#find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
#message("-- Python: " ${Python3_EXECUTABLE})
#message("-- Python Lib: " ${Python3_LIBRARIES})
#add_compile_definitions(PYTHON_PATH=\"${Python3_EXECUTABLE}\")

option(USE_CUDA OFF)

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
    find_package(onnxruntime REQUIRED)
    set(onnxruntime_ROOT ${onnxruntime_DIR}/../../..)
    include_directories(${onnxruntime_ROOT}/include)
    link_directories(${onnxruntime_ROOT}/lib/)
    message("ONNX include DIR:" ${onnxruntime_ROOT}/include)
endif (USE_CUDA)

include_directories(include third_party/include ${CUDA_INCLUDE_DIRS} ${TensorRT_INCLUDE_DIRS})
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

if (WIN32)
set(DEBUG_LINK_LIBS ws2_32 xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se nngd fmtlog-static)
set(OPTIMIZED_LINK_LIBS ws2_32 xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se nng fmtlog)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
#if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
target_link_libraries(${PROJECT_NAME} ${DEBUG_LINK_LIBS})
#else ()
#target_link_libraries(${PROJECT_NAME} ${OPTIMIZED_LINK_LIBS})
#endif()
else(WIN32)
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
add_compile_options(-Wl,-rpath=.)
endif()

target_link_libraries(${PROJECT_NAME} crypto ssl curl -pthread SQLiteWrapper pugixml nng
    xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se papi
)

endif(WIN32)

set(FMT_TEST OFF)
set(FMT_DOC OFF) 
add_subdirectory(third_party/fmt)
add_subdirectory(third_party/DataFrame)
target_link_libraries(${PROJECT_NAME} fmt::fmt DataFrame::DataFrame ta-lib xgboost)
if (USE_CUDA)
    target_link_libraries(${PROJECT_NAME} ${CUDA_LIBRARIES} ${TensorRT_LIBRARIES})
else (USE_CUDA) # onnxruntime
    # target_link_libraries(${PROJECT_NAME} onnxruntime::onnxruntime)
    target_link_libraries(${PROJECT_NAME} onnxruntime)
endif (USE_CUDA)
# install(TARGETS ${PROJECT_NAME})
# install(FILES
#     ${CMAKE_SOURCE_DIR}/build/dash_server
#     ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libxtpquoteapi.so
#     ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libxtptraderapi.so
#     TYPE BIN)
