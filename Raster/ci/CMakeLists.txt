cmake_minimum_required(VERSION 4.3)

# 独立 CI 入口 —— 不依赖 Qt/UI/Main，只构建 Raster + 它的测试。
# 本文件复用 Config.cmake 的统一配置（路径、测试开关等），通过 SANYI_SKIP_PATH_CHECK
# 跳过 VCPKG/Qt 路径检查（因为本入口只需要 OpenCV + GTest）。
# Raster 不链接 Qt：允许在没有 Qt 的机器 / CI 上配置。
set(SANYI_ALLOW_MISSING_QT ON)

# Config.cmake 里的 VCPKG_DIR 默认值是本机路径，CI 上不存在。
# 优先用 CI 注入的 VCPKG_ROOT 覆盖，否则回落到本地默认路径。
if(DEFINED ENV{VCPKG_ROOT} AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(VCPKG_DIR "$ENV{VCPKG_ROOT}" CACHE PATH "vcpkg root (from VCPKG_ROOT)")
endif()

# 设置 SANYI_ROOT（通常由 StandaloneInit.cmake 设置，但本入口不加载它）
get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SANYI_ROOT "${SANYI_ROOT}" CACHE INTERNAL "SanYi CAD project root")

include("${SANYI_ROOT}/Config.cmake")

# C 语言：模块里有一项"公开头必须是纯 C 头"的编译期契约测试，
# 需要真正的 C 编译器。enable_language() 必须在顶层目录调用，所以这里启用。
project(RasterCI LANGUAGES CXX C)

# C++ 标准已在 Config.cmake 中统一设置 (CMAKE_CXX_STANDARD 17)，此处不再重复。

# 确保本入口的测试开关为 ON（Config.cmake 默认值为 OFF）
# 注意：这里使用 FORCE 确保 Config.cmake 中的默认值被覆盖，使独立 CI 构建总是运行测试
set(BUILD_RASTER_TESTS ON CACHE BOOL "Build Raster unit tests" FORCE)

# CI 入口要能产出可交付的 SDK（跨平台门禁会拿它装出来再被外部工程消费），
# 所以这里把 SDK 安装打开。独立构建入口 Raster/CMakeLists.txt 也是默认开的。
set(RASTER_SDK_INSTALL ON CACHE BOOL "Install the deliverable SDK" FORCE)

find_package(OpenCV REQUIRED)
find_package(GTest CONFIG REQUIRED)

# enable_testing() 必须在 add_subdirectory **之前**调用：
# 它负责让 CTestTestfile.cmake 在各级子目录里生成，晚一步 add_test() 就会
# 变成"注册了但 ctest 看不到"——构建通过、测试一个都没跑，是最坑的一种失败。
enable_testing()
include(GoogleTest)

add_subdirectory("${SANYI_ROOT}/Raster/Raster" "${CMAKE_BINARY_DIR}/raster")

message(STATUS "[Raster CI]")
message(STATUS "  OpenCV: ${OpenCV_VERSION}")
message(STATUS "  GTest: found")
message(STATUS "  Targets: RasterTests, RasterCApiCompatTest")
