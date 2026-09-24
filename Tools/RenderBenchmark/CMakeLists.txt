# ============================================================================
# 渲染帧路径基准
# ============================================================================
# 开关（BUILD_RENDER_BENCHMARK）由根 Config.cmake 统一定义 —— 本文件不重复定义。
# 产物：SanYiRenderBenchmark（命令行程序，走 RenderX 的 Null 后端，无 GPU / 无窗口依赖）
#
# 用法：
#   cmake --build build --config Release --target SanYiRenderBenchmark --parallel 1
#   ./build/bin_Qt6/Release/SanYiRenderBenchmark.exe 1000000 render-baseline.csv after-lod
#
# 为什么只链接 RenderX：它自带 Null 后端（完整语义实现），且刻意不依赖本仓库的
# Log 等业务库，因此工具侧只需要一个 RenderX.dll。
# ============================================================================

if(NOT BUILD_RENDER_BENCHMARK)
    return()
endif()

add_executable(SanYiRenderBenchmark main.cpp)

target_compile_features(SanYiRenderBenchmark PRIVATE cxx_std_17)

target_include_directories(SanYiRenderBenchmark PRIVATE
    "${SANYI_ROOT}/Renderx/include"
)

target_link_libraries(SanYiRenderBenchmark PRIVATE
    RenderX
)

if(MSVC)
    target_compile_options(SanYiRenderBenchmark PRIVATE /utf-8 /FS)
endif()

set_property(TARGET SanYiRenderBenchmark PROPERTY FOLDER "Tools")
