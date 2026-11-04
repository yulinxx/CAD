# ============================================================================
# 百万级图元性能基准
# ============================================================================
# 开关（BUILD_PERF_BENCHMARK）由根 Config.cmake 统一定义 —— 本文件不重复定义。
# 产物：SanYiPerfBenchmark（命令行程序，无 Qt GUI 依赖）
#
# 用法：
#   cmake --build build --config Debug --target SanYiPerfBenchmark
#   ./build/bin_Qt6/Debug/SanYiPerfBenchmark.exe 1000000 baseline.csv
# ============================================================================

if(NOT BUILD_PERF_BENCHMARK)
    return()
endif()

add_executable(SanYiPerfBenchmark main.cpp)

target_compile_features(SanYiPerfBenchmark PRIVATE cxx_std_17)

target_include_directories(SanYiPerfBenchmark PRIVATE
    "${SANYI_ROOT}/Engine/2D/Include"
    "${SANYI_ROOT}/Engine/Common/Include"
    "${SANYI_ROOT}/Log/Log/Include"
    "${SANYI_ROOT}/Utility/Utility/Include"
)

target_link_libraries(SanYiPerfBenchmark PRIVATE
    Engine2D
    EngineCommon
    Log
    Utility
)

# BUILD_CAM 时追加 CAM 契约基准（输入构造/深拷贝/compile 耗时与内存估算）
if(BUILD_CAM)
    target_sources(SanYiPerfBenchmark PRIVATE CamBench.cpp)
    target_link_libraries(SanYiPerfBenchmark PRIVATE CAM)
    target_include_directories(SanYiPerfBenchmark PRIVATE
        "${SANYI_ROOT}/CAM/CAM/Include"
    )
    target_compile_definitions(SanYiPerfBenchmark PRIVATE SANYI_CAM_BENCH)
endif()

if(MSVC)
    target_compile_options(SanYiPerfBenchmark PRIVATE /utf-8 /FS)
endif()

sanyi_deploy_qt_dlls(SanYiPerfBenchmark)

set_property(TARGET SanYiPerfBenchmark PROPERTY FOLDER "Tools")
