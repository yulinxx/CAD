cmake_minimum_required(VERSION 4.3)

# ============================================================================
# License —— 独立构建入口
# ============================================================================
# 集成构建：根 CMakeLists.txt → add_subdirectory(License/License)（内层）。
# 独立构建：本文件提供 project() 与环境配置，把 License 单独拉出来开发 / 跑测试。
#
# 三条设计约束同时成立：
#
#   1) 复用仓库统一配置，不自己手搓一套，避免与主工程漂移：
#        ../Config.cmake               —— vcpkg / Qt 路径、MSVC 运行库、模块开关
#        ../CMake/StandaloneInit.cmake —— SANYI_ROOT、编译选项、GTest 查找、兄弟模块拉入
#      与 FileIO / Log / Utility / Nesting 的独立入口保持同一套写法。
#
#   2) 可脱离主程序单独编译：依赖的兄弟 DLL（Log）由本工程按需 add_subdirectory
#      拉进来构建，Main / UI / Engine 一概不参与。
#
#   3) 测试由内层 License/License/CMakeLists.txt 在 BUILD_LICENSE_TESTS=ON 时挂载，
#      本文件不得重复 add_subdirectory(Test)，否则 target 重定义。
#
# 用法：
#   cmake -S License -B License/build
#   cmake --build License/build --config Release
#   ctest --test-dir License/build -C Release
#   cmake -S License -B License/build -DBUILD_LICENSE_TESTS=OFF   # 只编库、不编测试
#   cmake -S License -B License/build -DBUILD_LICENSE_EXAMPLE=OFF # 不编示例
# ============================================================================

include(${CMAKE_CURRENT_LIST_DIR}/../Config.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/../CMake/StandaloneInit.cmake)

project(SanYiLicense
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD License Verification Library"
    LANGUAGES CXX
)

# MSVC 的 /W4 等选项必须在 project() 之后才能正确判定编译器
sanyi_setup_compiler_options()

# 示例目标仅存在于独立构建（内层不负责 Example）
option(BUILD_LICENSE_EXAMPLE "Build License usage example" ON)

# --- 测试与兄弟模块 ---------------------------------------------------------
# 先关兄弟测试、再探测 GTest（缺失时把测试开关统一降级为 OFF），
# 两步都必须发生在拉入兄弟模块之前，否则兄弟内层的 find_package(GTest REQUIRED)
# 会在降级前硬失败。
sanyi_only_own_tests(BUILD_LICENSE_TESTS)
if(BUILD_LICENSE_TESTS)
    sanyi_find_gtest()
endif()

# License 只依赖 Log（诊断输出）；OpenSSL 走 vcpkg，不涉及源码模块。
sanyi_add_sibling(Log "${SANYI_LOG_DIR}")

# 内层：创建 License DLL、挂载测试、输出状态
add_subdirectory(License)

# --- 示例（独立构建专属） ---------------------------------------------------
if(BUILD_LICENSE_EXAMPLE)
    add_subdirectory(Example)
endif()

# --- 独立构建摘要 -----------------------------------------------------------
message(STATUS "")
message(STATUS "========================================")
message(STATUS "  [Standalone: ${PROJECT_NAME}]")
message(STATUS "========================================")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build Tests:    ${BUILD_LICENSE_TESTS}")
message(STATUS "  Build Example:  ${BUILD_LICENSE_EXAMPLE}")
message(STATUS "  Output:         ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "========================================")
message(STATUS "")
