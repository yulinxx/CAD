# ============================================================================
# Config.cmake - SanYi CAD 全局构建配置
# ============================================================================
#
# 本文件是项目的核心配置文件，定义所有编译环境相关的选项。
# 使用方法：
#   cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
#   cmake --build build --config Release
#
# 常用配置示例：
#   # macOS 开发构建
#   cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
#
#   # Windows 发布构建
#   cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
#
#   # 启用所有可选模块
#   cmake -B build -S . -DBUILD_VISION=ON -DBUILD_HARDWARE=ON -DBUILD_NESTING=ON
#
# ============================================================================
#
# 本文件是路径配置与测试开关的**唯一配置处**：
#   - 路径：VCPKG_DIR / Qt_INSTALL_DIR / 工具链 / 输出目录，全部只在这里定义；
#   - 测试开关：所有 BUILD_*_TESTS 只在这里定义。
# 其它 CMakeLists.txt 一律不得重复定义同名项 —— 重复定义会互相覆盖，
# 且普通 set() 会遮蔽同名 option()（CMP0077），使命令行 -D 传参静默失效。
#
# 重复 include 保护：本文件既被根 CMakeLists.txt 包含，也被各模块的独立构建
# 入口包含（如 FileIO、Engraving/ci）。同一次 configure 内只执行一次，避免下面的
# CACHE ... FORCE 反复改写缓存并重复刷屏。
# 用**普通变量**而非缓存变量：缓存会跨 configure 持久化，导致下次 configure
# 本文件被直接跳过、文件里的改动不生效。
if(SANYI_CONFIG_INCLUDED)
    return()
endif()
set(SANYI_CONFIG_INCLUDED TRUE)

# --------------------------------------------------------------------
# 基础路径配置
# --------------------------------------------------------------------
# 路径解析顺序（从高到低）：
#   1. 已定义的 CMake 变量（-D 或上层 include）
#   2. 环境变量 VCPKG_DIR / Qt_INSTALL_DIR
#   3. Qt6_DIR 推导（仅 Qt）
#   4. 常见安装位置探测（无用户目录）
# 均未命中时给出明确错误，不写入任何用户机器相关的默认值。

# vcpkg 根目录
if(NOT DEFINED VCPKG_DIR OR VCPKG_DIR STREQUAL "")
    if(DEFINED ENV{VCPKG_DIR} AND NOT "$ENV{VCPKG_DIR}" STREQUAL "")
        set(VCPKG_DIR "$ENV{VCPKG_DIR}" CACHE PATH "VCPKG installation directory")
    else()
        # 常见安装位置（不含任何用户主目录）
        set(_vcpkg_candidates "")
        if(WIN32)
            list(APPEND _vcpkg_candidates
                "C:/vcpkg"
                "$ENV{ProgramFiles}/vcpkg"
                "$ENV{ProgramFiles\(x86\)}/vcpkg"
            )
        elseif(APPLE)
            list(APPEND _vcpkg_candidates "/usr/local/vcpkg" "/opt/vcpkg")
        else()
            list(APPEND _vcpkg_candidates "/usr/local/vcpkg" "/opt/vcpkg")
        endif()
        set(VCPKG_DIR "")
        foreach(_cand IN LISTS _vcpkg_candidates)
            if(EXISTS "${_cand}/scripts/buildsystems/vcpkg.cmake")
                set(VCPKG_DIR "${_cand}" CACHE PATH "VCPKG installation directory")
                break()
            endif()
        endforeach()
        if(VCPKG_DIR STREQUAL "")
            set(VCPKG_DIR "" CACHE PATH "VCPKG installation directory (set via VCPKG_DIR env or -DVCPKG_DIR=...)")
        endif()
    endif()
endif()

# Qt 安装目录
if(NOT DEFINED Qt_INSTALL_DIR OR Qt_INSTALL_DIR STREQUAL "")
    if(DEFINED ENV{Qt_INSTALL_DIR} AND NOT "$ENV{Qt_INSTALL_DIR}" STREQUAL "")
        set(Qt_INSTALL_DIR "$ENV{Qt_INSTALL_DIR}" CACHE PATH "Qt installation directory")
    elseif(DEFINED Qt6_DIR)
        get_filename_component(_qt6_root "${Qt6_DIR}" DIRECTORY)
        get_filename_component(Qt_INSTALL_DIR "${_qt6_root}" DIRECTORY)
        set(Qt_INSTALL_DIR "${Qt_INSTALL_DIR}" CACHE PATH "Qt installation directory")
    else()
        # 常见安装位置（不含任何用户主目录）
        set(_qt_candidates "")
        if(WIN32)
            list(APPEND _qt_candidates
                "C:/Qt"
                "C:/Program Files/Qt"
                "$ENV{ProgramFiles}/Qt"
            )
            # 在 C:/Qt 下探测最新 6.x 安装
            if(EXISTS "C:/Qt")
                file(GLOB _qt_ver_dirs "C:/Qt/6.*")
                list(SORT _qt_ver_dirs ORDER DESCENDING)
                foreach(_ver IN LISTS _qt_ver_dirs)
                    get_filename_component(_ver_name "${_ver}" NAME)
                    list(APPEND _qt_candidates
                        "${_ver}/msvc2022_64"
                        "${_ver}/msvc2019_64"
                    )
                endforeach()
            endif()
        elseif(APPLE)
            list(APPEND _qt_candidates "/usr/local/Qt" "/opt/Qt")
        else()
            list(APPEND _qt_candidates "/usr/local/Qt" "/opt/Qt" "/usr/lib/qt6")
        endif()
        set(Qt_INSTALL_DIR "")
        foreach(_cand IN LISTS _qt_candidates)
            if(EXISTS "${_cand}/lib/cmake/Qt6/Qt6Config.cmake")
                set(Qt_INSTALL_DIR "${_cand}" CACHE PATH "Qt installation directory")
                break()
            endif()
        endforeach()
        if(Qt_INSTALL_DIR STREQUAL "")
            set(Qt_INSTALL_DIR "" CACHE PATH "Qt installation directory (set via Qt_INSTALL_DIR env or -DQt_INSTALL_DIR=...)")
        endif()
    endif()
endif()

# Qt 主版本号
set(QT_VERSION_MAJOR 6)

# 路径未命中时明确失败，避免后续 configure 产生难懂的连锁错误
if(VCPKG_DIR STREQUAL "" OR NOT EXISTS "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")
    message(FATAL_ERROR
        "[Config] VCPKG_DIR not found or invalid: '${VCPKG_DIR}'\n"
        "  Set environment variable VCPKG_DIR, or pass -DVCPKG_DIR=<path> to cmake.")
endif()
if(Qt_INSTALL_DIR STREQUAL "" OR NOT EXISTS "${Qt_INSTALL_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
    message(FATAL_ERROR
        "[Config] Qt_INSTALL_DIR not found or invalid: '${Qt_INSTALL_DIR}'\n"
        "  Set environment variable Qt_INSTALL_DIR, or pass -DQt_INSTALL_DIR=<path> to cmake.")
endif()

# --------------------------------------------------------------------
# vcpkg 工具链
# --------------------------------------------------------------------
# 必须在 project() 之前生效 —— 根脚本在 project() 之前 include 本文件，正是为此。
# 各模块不得再自行 set(CMAKE_TOOLCHAIN_FILE ...)。
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file")

# --------------------------------------------------------------------
# 构建类型配置
# --------------------------------------------------------------------
# 可选值: Debug, Release, RelWithDebInfo, MinSizeRel
# 默认使用 Release 以获得最佳性能
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type: Debug|Release|RelWithDebInfo|MinSizeRel" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

message(STATUS "======================================================================")
message(STATUS "[Config] Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "======================================================================")

# --------------------------------------------------------------------
# 编译器配置
# --------------------------------------------------------------------

# MSVC 特定配置
if(MSVC)
    # 动态链接 CRT (MD/MDd)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime library" FORCE)

    # UTF-8 编码支持
    add_compile_options(/utf-8)

    # 关闭 CRT 迭代扩展警告 (vcpkg 依赖大量模板导致)
    add_compile_options(/wd4503)

    message(STATUS "[Config] Compiler: MSVC ${MSVC_VERSION}")
endif()

# GCC/Clang 特定配置
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # 统一警告级别
    add_compile_options(-Wall -Wextra -Wpedantic)

    # 额外警告抑制 (跨平台兼容)
    add_compile_options(
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-unused-private-field
        -Wno-unused-lambda-capture
        -Wno-nullability-extension
        -Wno-reorder
    )

    # macOS 特定
    if(APPLE)
        # 抑制大小写不敏感路径警告
        add_compile_options(-Wno-nonportable-include-path)
        # 抑制 OpenGL 废弃警告（使用 Metal 作为后端）
        add_compile_options(-Wno-deprecated-declarations)
    endif()

    message(STATUS "[Config] Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# --------------------------------------------------------------------
# C++ 标准配置
# --------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------------------------
# 符号可见性控制
# --------------------------------------------------------------------
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# --------------------------------------------------------------------
# Unity Build 配置 (可加速编译)
# --------------------------------------------------------------------
option(SANYI_UNITY_BUILD "Enable unity build (faster compilation)" ON)
if(SANYI_UNITY_BUILD)
    set(CMAKE_UNITY_BUILD ON)
    # Unity 构建的块大小，可根据内存调整
    set(CMAKE_UNITY_BUILD_BATCH_SIZE 16)
    # 默认排除 Objective-C++，避免与 C++ 混在一个 unity 块里导致编译错误
    list(APPEND CMAKE_UNITY_BUILD_SUPPORTED_SOURCE_EXTENSIONS ".mm")
endif()

# --------------------------------------------------------------------
# 输出目录配置
# --------------------------------------------------------------------
# 统一带 Qt 版本后缀，避免同机多 Qt 版本的产物互相覆盖。
# 这是本项目输出目录的唯一配置处：各模块不得再自行 set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ...)。
set(_SANYI_BIN_DIR "${CMAKE_BINARY_DIR}/bin_Qt${QT_VERSION_MAJOR}")
set(_SANYI_LIB_DIR "${CMAKE_BINARY_DIR}/lib_Qt${QT_VERSION_MAJOR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}" CACHE PATH "Runtime output directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}" CACHE PATH "Library output directory")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_SANYI_LIB_DIR}" CACHE PATH "Archive output directory")

# 多配置生成器（Visual Studio / Xcode）需要再按配置分子目录
foreach(_sanyi_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_sanyi_cfg}" _sanyi_cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_sanyi_cfg_u} "${_SANYI_BIN_DIR}/${_sanyi_cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_sanyi_cfg_u} "${_SANYI_BIN_DIR}/${_sanyi_cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_sanyi_cfg_u} "${_SANYI_LIB_DIR}/${_sanyi_cfg}")
endforeach()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# --------------------------------------------------------------------
# Qt 配置
# --------------------------------------------------------------------
set(Qt6_DIR "${Qt_INSTALL_DIR}/lib/cmake/Qt6" CACHE PATH "Qt CMake configuration directory")
list(INSERT CMAKE_PREFIX_PATH 0 "${Qt_INSTALL_DIR}")

# Windows: 配置 PATH 以包含 Qt 和 vcpkg 二进制目录
if(WIN32 AND EXISTS "${Qt_INSTALL_DIR}")
    set(_SANYI_QT_BIN "${Qt_INSTALL_DIR}/bin")
    set(_SANYI_VCPKG_BIN "${VCPKG_DIR}/installed/x64-windows/bin")
    set(ENV{PATH} "${_SANYI_QT_BIN};${_SANYI_VCPKG_BIN};$ENV{PATH}")
    set(QT_PLUGIN_PATH "${Qt_INSTALL_DIR}/plugins" CACHE PATH "Qt plugins directory")
endif()

# --------------------------------------------------------------------
# 模块编译开关配置
# --------------------------------------------------------------------
# 默认值: 可选模块默认开启，核心模块默认关闭
if(NOT DEFINED SANYI_DEFAULT_OPTIONAL)
    set(SANYI_DEFAULT_OPTIONAL ON)
endif()
if(NOT DEFINED SANYI_DEFAULT_CORE)
    set(SANYI_DEFAULT_CORE OFF)
endif()

# ===== 渲染后端配置 =====
set(SANYI_RENDER_BACKENDS "OPENGL;METAL;VULKAN" CACHE STRING "Available render backends")
if(APPLE)
    set(SANYI_DEFAULT_RENDER_BACKEND "METAL" CACHE STRING "Default render backend: OPENGL|METAL|VULKAN")
else()
    set(SANYI_DEFAULT_RENDER_BACKEND "OPENGL" CACHE STRING "Default render backend: OPENGL|METAL|VULKAN")
endif()

# ===== UI 维度配置 =====
set(SANYI_UI_DIMENSIONS "2D;3D" CACHE STRING "Available UI dimensions")
set(SANYI_DEFAULT_UI_DIMENSION "2D" CACHE STRING "Default UI dimension: 2D|3D")

# ===== 核心功能模块 =====
# 建议保持默认开启状态
option(BUILD_RENDERX "Build Renderx rendering engine (3D rendering core)" ON)
option(BUILD_UI2D "Build UI2D module (2D user interface, always ON)" ON)

# ---- UI 维度门控（唯一权威开关）----
# 用法：cmake -DBUILD_UI_DIMENSION=2D
#   BOTH（默认）：UI2D + UI3D 都构建（保持历史行为）
#   2D          ：关闭 UI3D，只产出 2D 目标（Main 自动走 UI3D 桩头文件回退）
#   3D          ：构建 UI3D，并把运行时默认视口设为 3D
# 注意：UI2D 是全应用必需（Main 有数百处 UI2D 引用），任何取值都不会关闭它；
#       因此 "3D" 表示"启用 3D 且默认 3D"，而非"仅 3D"。
# BUILD_UI3D / SANYI_DEFAULT_UI_DIMENSION 均由本开关派生（FORCE 写回缓存，
# 避免 2D→BOTH 切换时缓存粘滞）。
set(BUILD_UI_DIMENSION "BOTH" CACHE STRING "UI build dimension gate: 2D|3D|BOTH")
set_property(CACHE BUILD_UI_DIMENSION PROPERTY STRINGS 2D 3D BOTH)
if(BUILD_UI_DIMENSION STREQUAL "2D")
    set(BUILD_UI3D OFF CACHE BOOL "Build UI3D module (derived from BUILD_UI_DIMENSION)" FORCE)
    set(SANYI_DEFAULT_UI_DIMENSION "2D" CACHE STRING "Default UI dimension (derived)" FORCE)
elseif(BUILD_UI_DIMENSION STREQUAL "3D")
    set(BUILD_UI3D ON CACHE BOOL "Build UI3D module (derived from BUILD_UI_DIMENSION)" FORCE)
    set(SANYI_DEFAULT_UI_DIMENSION "3D" CACHE STRING "Default UI dimension (derived)" FORCE)
elseif(BUILD_UI_DIMENSION STREQUAL "BOTH")
    set(BUILD_UI3D ON CACHE BOOL "Build UI3D module (derived from BUILD_UI_DIMENSION)" FORCE)
    set(SANYI_DEFAULT_UI_DIMENSION "2D" CACHE STRING "Default UI dimension (derived)" FORCE)
else()
    message(FATAL_ERROR
        "[SanYi] BUILD_UI_DIMENSION 取值非法: '${BUILD_UI_DIMENSION}'（应为 2D|3D|BOTH）")
endif()

option(BUILD_NESTING "Build Nesting module (2D/3D nesting/arrangement algorithm)" ON)
option(BUILD_CAM "Build CAM module (laser cutting toolpath generation)" OFF)

# ===== 可选功能模块 =====
# 按需启用
option(BUILD_VISION "Build Vision module (image processing, computer vision)" OFF)
option(BUILD_NETWORK "Build Network module (HTTP, WebSocket, cloud sync)" OFF)
option(BUILD_HARDWARE "Build Hardware module (laser control, material database)" OFF)
option(BUILD_ENGRAVING "Build Engraving module (3D laser engraving)" OFF)
option(BUILD_GEOMODELCORE "Build GeoModelCore module (OpenCASCADE-based geometry modeling)" OFF)
option(BUILD_CRASHHANDLER "Build CrashHandler module (crash capture and reporting)" ON)
option(BUILD_PYTHON "Build Python module (PythonHost integration framework)" OFF)

# ===== 工具 =====
option(BUILD_KEYGEN_TOOL "Build KeygenTool (offline license key generator)" OFF)
# 百万级图元性能基准（Tools/PerfBenchmark）。默认构建，便于随时复测与优化前后对比；
# 本项按 CMake 常规：首次 configure 写入缓存，需要关闭时用 -DBUILD_PERF_BENCHMARK=OFF。
option(BUILD_PERF_BENCHMARK "Build million-entity performance benchmark" ON)
# 渲染帧路径基准（Tools/RenderBenchmark）。走 RenderX 的 Null 后端，
# 不依赖 GPU 与窗口系统，可无人值守复现，默认构建。
option(BUILD_RENDER_BENCHMARK "Build headless render frame-path benchmark" ON)

# --------------------------------------------------------------------
# 测试开关（本项目测试开关的唯一配置处）
# --------------------------------------------------------------------
# 规则：所有 BUILD_*_TESTS 只在本文件定义。其它 CMakeLists.txt 一律不得再
#       用 option() / set() 定义同名开关，因为：
#         - 重复定义会互相覆盖，谁先定义谁生效，排查成本极高；
#         - 普通 set() 会遮蔽同名 option()（CMP0077），使 -D 传参静默失效。
#
# 取值方式：下面每个开关都是**显式取值**，本文件即权威来源，改完重新 configure
#           立即生效（用 FORCE 写回缓存，避免旧缓存粘滞导致「改了文件没反应」）。

# 便捷总开关：ON = 把下面全部模块测试一并打开；OFF = 保持各模块的显式取值
set(BUILD_ALL_TESTS OFF CACHE BOOL "打开全部模块测试（便捷总开关）" FORCE)

# ---- 逐模块测试开关（当前取值沿用收敛前的既有行为，可按需手动调整）----
set(BUILD_UTILITY_TESTS          OFF CACHE BOOL "Utility 单元测试" FORCE)
set(BUILD_LOG_TESTS              OFF CACHE BOOL "Log 单元测试" FORCE)
set(BUILD_ENGINE2D_TESTS         OFF CACHE BOOL "Engine2D 单元测试" FORCE)
set(BUILD_ENGINE3D_TESTS         OFF CACHE BOOL "Engine3D 单元测试" FORCE)
set(BUILD_GEOMODELCORE_TESTS     OFF CACHE BOOL "GeoModelCore 单元测试" FORCE)
set(BUILD_FILEIO_TESTS           OFF CACHE BOOL "FileIO 单元测试" FORCE)
set(BUILD_LICENSE_TESTS          ON  CACHE BOOL "License 单元测试" FORCE)
set(BUILD_NESTING_TESTS          OFF CACHE BOOL "Nesting 单元测试" FORCE)
set(BUILD_CAM_TESTS              OFF CACHE BOOL "CAM 单元测试" FORCE)
set(BUILD_ENGRAVING_TESTS        OFF CACHE BOOL "Engraving 单元测试" FORCE)
set(BUILD_HARDWARE_TESTS         OFF CACHE BOOL "Hardware 单元测试" FORCE)
set(BUILD_VISION_TESTS           OFF CACHE BOOL "Vision 单元测试" FORCE)
set(BUILD_PYTHONHOST_TESTS       OFF CACHE BOOL "PythonHost 单元测试" FORCE)
set(BUILD_CRASHHANDLER_TESTS     OFF CACHE BOOL "CrashHandler 单元测试" FORCE)
set(BUILD_RENDERX_TESTS          OFF CACHE BOOL "Renderx 单元测试" FORCE)
set(BUILD_RENDERBRIDGE_TESTS     ON  CACHE BOOL "RenderBridge 契约测试" FORCE)
set(BUILD_UI_COMMON_TESTS        ON  CACHE BOOL "UICommon 单元测试" FORCE)
set(BUILD_UI2D_TESTS             OFF CACHE BOOL "UI2D 单元测试" FORCE)
set(BUILD_UI3D_TESTS             ON  CACHE BOOL "UI3D 单元测试" FORCE)
set(BUILD_MAIN_TESTS             OFF CACHE BOOL "Main 单元测试（含撤销/重做回归）" FORCE)
set(BUILD_VIEWPORT_REFRESH_TESTS ON  CACHE BOOL "2D 视口刷新契约测试" FORCE)
set(BUILD_SCENE_EDIT_TESTS       ON  CACHE BOOL "场景编辑集成回归测试（撤销状态交换链路）" FORCE)

# 全部模块测试开关清单：供总开关应用、GTest 缺失统一降级、以及状态输出使用
set(SANYI_TEST_SWITCHES
    BUILD_UTILITY_TESTS
    BUILD_LOG_TESTS
    BUILD_ENGINE2D_TESTS
    BUILD_ENGINE3D_TESTS
    BUILD_GEOMODELCORE_TESTS
    BUILD_FILEIO_TESTS
    BUILD_LICENSE_TESTS
    BUILD_NESTING_TESTS
    BUILD_CAM_TESTS
    BUILD_ENGRAVING_TESTS
    BUILD_HARDWARE_TESTS
    BUILD_VISION_TESTS
    BUILD_PYTHONHOST_TESTS
    BUILD_CRASHHANDLER_TESTS
    BUILD_RENDERX_TESTS
    BUILD_RENDERBRIDGE_TESTS
    BUILD_UI_COMMON_TESTS
    BUILD_UI2D_TESTS
    BUILD_UI3D_TESTS
    BUILD_MAIN_TESTS
    BUILD_VIEWPORT_REFRESH_TESTS
    BUILD_SCENE_EDIT_TESTS
    CACHE INTERNAL "全部单元测试开关")

# 总开关为 ON 时覆盖上面所有逐模块取值
if(BUILD_ALL_TESTS)
    foreach(_sanyi_test IN LISTS SANYI_TEST_SWITCHES)
        set(${_sanyi_test} ON CACHE BOOL "opened by BUILD_ALL_TESTS" FORCE)
    endforeach()
endif()

# --------------------------------------------------------------------
# 依赖库查找配置
# --------------------------------------------------------------------

# 渲染后端选择逻辑
if(SANYI_DEFAULT_RENDER_BACKEND STREQUAL "METAL")
    if(APPLE)
        # Metal 是 macOS 系统框架，不需要 find_package
        message(STATUS "[Render] Backend: Metal (Apple)")
    else()
        message(FATAL_ERROR "[Render] Metal backend only supported on macOS")
    endif()
elseif(SANYI_DEFAULT_RENDER_BACKEND STREQUAL "VULKAN")
    if(SANYI_ENABLE_VULKAN)
        find_package(Vulkan REQUIRED)
        message(STATUS "[Render] Backend: Vulkan")
    else()
        message(FATAL_ERROR "[Render] Vulkan support disabled (SANYI_ENABLE_VULKAN=OFF)")
    endif()
else()
    # OpenGL 在 macOS 上已被废弃，使用 Metal 作为默认后端
    if(APPLE)
        message(STATUS "[Render] Backend: Metal (fallback from OpenGL on macOS)")
    else()
        find_package(OpenGL REQUIRED)
        message(STATUS "[Render] Backend: OpenGL")
    endif()
endif()

# Vulkan 支持 (可选)
option(SANYI_ENABLE_VULKAN "Enable Vulkan rendering support (optional)" ON)

# OpenCASCADE 配置 (GeoModelCore 需要)
if(BUILD_GEOMODELCORE)
    # OpenCASCADE 通常通过 vcpkg 安装
    # 如需指定自定义路径，取消注释下面一行:
    # set(OpenCASCADE_DIR "${VCPKG_DIR}/installed/x64-windows/share/opencascade")
endif()

# --------------------------------------------------------------------
# 最终配置汇总
# --------------------------------------------------------------------
message(STATUS "")
message(STATUS "[Qt]")
message(STATUS "  Install Directory: ${Qt_INSTALL_DIR}")
message(STATUS "  Config Directory: ${Qt6_DIR}")
message(STATUS "[vcpkg]")
message(STATUS "  VCPKG_DIR: ${VCPKG_DIR}")
message(STATUS "[Render Backend]")
message(STATUS "  Default: ${SANYI_DEFAULT_RENDER_BACKEND}")
message(STATUS "[UI Dimension]")
message(STATUS "  Default: ${SANYI_DEFAULT_UI_DIMENSION}")
message(STATUS "[Modules]")
message(STATUS "  RenderX:        ${BUILD_RENDERX}")
message(STATUS "  UI2D:           ${BUILD_UI2D}")
message(STATUS "  UI3D:           ${BUILD_UI3D}")
message(STATUS "  Vision:         ${BUILD_VISION}")
message(STATUS "  Network:        ${BUILD_NETWORK}")
message(STATUS "  Hardware:       ${BUILD_HARDWARE}")
message(STATUS "  Engraving:      ${BUILD_ENGRAVING}")
message(STATUS "  Nesting:        ${BUILD_NESTING}")
message(STATUS "  CAM:            ${BUILD_CAM}")
message(STATUS "  GeoModelCore:   ${BUILD_GEOMODELCORE}")
message(STATUS "  CrashHandler:   ${BUILD_CRASHHANDLER}")
message(STATUS "  Python:         ${BUILD_PYTHON}")
message(STATUS "[Tests]")
message(STATUS "  Master switch (BUILD_ALL_TESTS): ${BUILD_ALL_TESTS}")
set(_sanyi_enabled_tests "")
foreach(_sanyi_test IN LISTS SANYI_TEST_SWITCHES)
    if(${_sanyi_test})
        list(APPEND _sanyi_enabled_tests "${_sanyi_test}")
    endif()
endforeach()
if(_sanyi_enabled_tests)
    message(STATUS "  Enabled: ${_sanyi_enabled_tests}")
else()
    message(STATUS "  Enabled: none")
endif()
message(STATUS "")
