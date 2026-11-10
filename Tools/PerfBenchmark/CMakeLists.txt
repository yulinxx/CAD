# ============================================================================
# 百万级图元性能基准
# ============================================================================
# 开关（BUILD_PERF_BENCHMARK）由根 Config.cmake 统一定义 —— 本文件不重复定义。
# 产物：AprPerfBenchmark（命令行程序，无 Qt GUI 依赖）
#
# 用法：
#   cmake --build build --config Debug --target AprPerfBenchmark
#   ./build/bin_Qt6/Debug/AprPerfBenchmark.exe 1000000 baseline.csv
# ============================================================================

if(NOT BUILD_PERF_BENCHMARK)
    return()
endif()

add_executable(AprPerfBenchmark main.cpp)

target_compile_features(AprPerfBenchmark PRIVATE cxx_std_17)

target_include_directories(AprPerfBenchmark PRIVATE
    "${APR_ROOT}/Engine/2D/Include"
    "${APR_ROOT}/Engine/Common/Include"
    "${APR_ROOT}/Log/Log/Include"
    "${APR_ROOT}/Utility/Utility/Include"
)

target_link_libraries(AprPerfBenchmark PRIVATE
    Engine2D
    EngineCommon
    Log
    Utility
)

# BUILD_CAM 时追加 CAM 契约基准（输入构造/深拷贝/compile 耗时与内存估算）
if(BUILD_CAM)
    target_sources(AprPerfBenchmark PRIVATE CamBench.cpp)
    target_link_libraries(AprPerfBenchmark PRIVATE CAM)
    target_include_directories(AprPerfBenchmark PRIVATE
        "${APR_ROOT}/CAM/CAM/Include"
    )
    target_compile_definitions(AprPerfBenchmark PRIVATE APR_CAM_BENCH)
endif()

if(MSVC)
    target_compile_options(AprPerfBenchmark PRIVATE /utf-8 /FS)
endif()

apr_deploy_qt_dlls(AprPerfBenchmark)

set_property(TARGET AprPerfBenchmark PROPERTY FOLDER "Tools")
