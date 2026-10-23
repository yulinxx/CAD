cmake_minimum_required(VERSION 4.3)

# =============================================================================
# Raster —— 独立构建入口
# =============================================================================
# 集成构建：根 CMakeLists.txt -> add_subdirectory(Raster/Raster)（内层）。
# 独立构建：本文件提供 project() 与环境配置，把 Raster 单独拉出来开发 / 打包。
#
# 三条设计约束同时成立：
#
#   1) 复用仓库统一配置，不自己手搓一套，避免与主工程漂移：
#        ../Config.cmake               -> vcpkg / Qt 路径、MSVC 运行库、模块开关
#        ../CMake/StandaloneInit.cmake -> SANYI_ROOT、编译选项、GTest 查找
#      与 Nesting / Engraving / Vision 的独立入口保持同一套写法。
#
#   2) 可脱离主程序单独编译：Raster 不链接任何兄弟 DLL，唯一的第三方依赖是
#      OpenCV（vcpkg）。没有 Qt、没有 Main、没有 UI。
#
#   3) 可独立交付：RASTER_SDK_INSTALL=ON 时产出完整 SDK（头文件 + 导入库 +
#      DLL + find_package 配置），供第三方工程直接消费。
#
# 用法：
#   # 开发构建
#   cmake -S Raster -B Raster/build
#   cmake --build Raster/build --config Release
#   ctest --test-dir Raster/build -C Release
#   # 只产出 SDK
#   cmake -S Raster -B Raster/build -DBUILD_RASTER_TESTS=OFF -DBUILD_RASTER_EXAMPLES=OFF
#   cmake --install Raster/build --config Release --prefix <out>
# =============================================================================

# Raster 不链接 Qt：允许在没有 Qt 的机器 / CI 上配置。
set(SANYI_ALLOW_MISSING_QT ON)

include(${CMAKE_CURRENT_LIST_DIR}/../Config.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../CMake/StandaloneInit.cmake)

project(SanYiRaster
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD Raster Bitmap Processing Library"
    LANGUAGES CXX C
)

# MSVC 的 /W4 等选项必须在 project() 之后才能正确判定编译器
sanyi_setup_compiler_options()

option(BUILD_RASTER_EXAMPLES "Build Raster examples" ON)
option(BUILD_RASTER_BENCHMARK "Build the Raster throughput benchmark" ON)
option(RASTER_SDK_INSTALL "Install the deliverable SDK (public headers + import lib + DLL)" ON)
option(RASTER_ABI_GUARD "Fail configuration when Raster public headers break the cross-DLL contract" ON)

# --- 测试 -------------------------------------------------------------------
sanyi_only_own_tests(BUILD_RASTER_TESTS)
if(BUILD_RASTER_TESTS)
    sanyi_find_gtest()
endif()

# 内层：创建 Raster DLL、挂载测试（BUILD_RASTER_TESTS 时）、输出状态
add_subdirectory(Raster)

# --- 示例 -------------------------------------------------------------------
if(BUILD_RASTER_EXAMPLES)
    add_subdirectory(Examples)
endif()

# --- 基准 -------------------------------------------------------------------
# 基准只消费公开 API，测出来的就是客户实际会感受到的性能。
if(BUILD_RASTER_BENCHMARK)
    set(RASTER_BENCHMARK_USE_LOCAL_TARGET ON)
    add_subdirectory(Tools/benchmark)
endif()

# --- 独立构建摘要 -----------------------------------------------------------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  [Standalone: ${PROJECT_NAME}]")
message(STATUS "========================================")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build Examples: ${BUILD_RASTER_EXAMPLES}")
message(STATUS "  Build Tests:    ${BUILD_RASTER_TESTS}")
message(STATUS "  SDK Install:    ${RASTER_SDK_INSTALL}")
message(STATUS "  Output:         ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
message(STATUS "")
