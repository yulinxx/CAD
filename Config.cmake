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

# --------------------------------------------------------------------
# 基础路径配置
# --------------------------------------------------------------------
# 以下路径可根据实际安装位置修改

# vcpkg 根目录
if(NOT DEFINED VCPKG_DIR OR VCPKG_DIR STREQUAL "")
    if(WIN32)
        # set(VCPKG_DIR "C:/vcpkg/" CACHE PATH "VCPKG installation directory")
        set(VCPKG_DIR "C:/Users/xx/vcpkg/" CACHE PATH "VCPKG installation directory")
    elseif(UNIX AND NOT APPLE)
        set(VCPKG_DIR "/usr/local/vcpkg/" CACHE PATH "VCPKG installation directory")
    elseif(APPLE)
        set(VCPKG_DIR "/Users/ms/vcpkg" CACHE PATH "VCPKG installation directory")
    endif()
endif()

# Qt 安装目录
if(NOT DEFINED Qt_INSTALL_DIR OR Qt_INSTALL_DIR STREQUAL "")
    if(WIN32)
        set(Qt_INSTALL_DIR "C:/Users/xx/Qt/6.11.2/msvc2022_64" CACHE PATH "Qt installation directory")
    elseif(UNIX AND NOT APPLE)
        set(Qt_INSTALL_DIR "/usr/local/Qt/6.11.1/gcc_64" CACHE PATH "Qt installation directory")
    elseif(APPLE)
        set(Qt_INSTALL_DIR "/Users/ms/Qt/6.11.1/macos" CACHE PATH "Qt installation directory")
    endif()
endif()

# Qt 主版本号
set(QT_VERSION_MAJOR 6)

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

    # 额外警告 (可根据需要调整)
    add_compile_options(
        -Wno-unused-parameter
        -Wno-unused-variable
        -Wno-nullability-extension
    )

    # macOS 特定
    if(APPLE)
        # 抑制大小写不敏感路径警告
        add_compile_options(-Wno-nonportable-include-path)
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
endif()

# --------------------------------------------------------------------
# 输出目录配置
# --------------------------------------------------------------------
# 注意: 输出目录由 CMakeLists.txt 统一管理 (带 Qt 版本后缀)
# 此处仅设置基础路径，实际路径在 CMakeLists.txt 中配置
# set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
# set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

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

# ===== 核心功能模块 =====
# 建议保持默认开启状态
option(BUILD_RENDERX "Build Renderx rendering engine (3D rendering core)" ON)
option(BUILD_UI3D "Build UI3D module (3D user interface)" ON)
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

# ===== 工具和测试 =====
option(BUILD_ALL_TESTS "Build all unit tests" OFF)
option(BUILD_KEYGEN_TOOL "Build KeygenTool (offline license key generator)" OFF)

# 禁用各模块测试 (避免测试代码与主代码冲突)
set(BUILD_RENDERX_TESTS OFF)
set(BUILD_UI2D_TESTS OFF)
set(BUILD_MAIN_TESTS OFF)

# --------------------------------------------------------------------
# 依赖库查找配置
# --------------------------------------------------------------------

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
message(STATUS "[Modules]")
message(STATUS "  RenderX:    ${BUILD_RENDERX}")
message(STATUS "  UI3D:       ${BUILD_UI3D}")
message(STATUS "  Vision:     ${BUILD_VISION}")
message(STATUS "  Network:    ${BUILD_NETWORK}")
message(STATUS "  Hardware:   ${BUILD_HARDWARE}")
message(STATUS "  Engraving:  ${BUILD_ENGRAVING}")
message(STATUS "  Nesting:    ${BUILD_NESTING}")
message(STATUS "  CAM:        ${BUILD_CAM}")
message(STATUS "  GeoModelCore: ${BUILD_GEOMODELCORE}")
message(STATUS "  CrashHandler: ${BUILD_CRASHHANDLER}")
message(STATUS "  Python:     ${BUILD_PYTHON}")
message(STATUS "[Tests]")
message(STATUS "  Unit Tests: ${BUILD_ALL_TESTS}")
message(STATUS "")
