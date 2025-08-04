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
include_directories(include third_party/include)

if (WIN32)
link_directories(${PROJECT_SOURCE_DIR}/third_party/libs/win)
else(WIN32)
link_directories(${PROJECT_SOURCE_DIR}/third_party/libs/unix ${PROJECT_BINARY_DIR}/_deps/curl-build/lib)
endif(WIN32)
# find_package(mlpack REQUIRED)

# file(GLOB_RECURSE prj_files_header include/*.h include/*.hpp)
file(GLOB_RECURSE prj_files_cpp src/*.cpp src/*.c) 
add_executable(${PROJECT_NAME} ${prj_files_cpp})

if (WIN32)
set(DEBUG_LINK_LIBS ws2_32 xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se nngd fmtlog-static)
set(OPTIMIZED_LINK_LIBS ws2_32 xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se nng fmtlog)
target_compile_definitions(${PROJECT_NAME} PRIVATE NOMINMAX)
#if (${CMAKE_BUILD_TYPE} STREQUAL "Debug")
target_link_libraries(${PROJECT_NAME} ${DEBUG_LINK_LIBS} )
#else ()
#target_link_libraries(${PROJECT_NAME} ${OPTIMIZED_LINK_LIBS})
#endif()
else(WIN32)
if (${CMAKE_BUILD_TYPE} STREQUAL "Release")
add_compile_options(-Wl,-rpath=.)
endif()

target_link_libraries(${PROJECT_NAME} crypto QuantLib ssl curl -pthread SQLiteWrapper pugixml nng
    xtpquoteapi xtptraderapi thostmduserapi_se thosttraderapi_se parquet papi
)

endif(WIN32)

set(FMT_TEST OFF)
set(FMT_DOC OFF) 
add_subdirectory(third_party/fmt)
add_subdirectory(third_party/DataFrame)
target_link_libraries(${PROJECT_NAME} fmt::fmt DataFrame::DataFrame ta-lib xgboost)

# install(TARGETS ${PROJECT_NAME})
# install(FILES
#     ${CMAKE_SOURCE_DIR}/build/dash_server
#     ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libxtpquoteapi.so
#     ${CMAKE_SOURCE_DIR}/third_party/libs/unix/libxtptraderapi.so
#     TYPE BIN)
