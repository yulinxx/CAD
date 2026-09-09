# ============================================================================
# Config.cmake — SanYi CAD 公共构建配置
# ============================================================================
# 本文件为公共配置，入库版本控制。
# 用户私有配置请编辑同目录下的 Config.local.cmake（不在版本控制中）。
# ============================================================================

# =============================================================================
# 【用户配置区域】- 以下配置可被 Config.local.cmake 覆盖
# =============================================================================

# -----------------------------------------------------------------------------
# [1] vcpkg 路径配置
# -----------------------------------------------------------------------------
if(NOT DEFINED VCPKG_DIR OR VCPKG_DIR STREQUAL "")
    if(WIN32)
        set(VCPKG_DIR "C:/Users/xx/vcpkg" CACHE PATH "vcpkg installation directory")
        # set(VCPKG_DIR "C:/Users/xx/vcpkg" CACHE PATH "vcpkg installation directory")
    elseif(UNIX AND NOT APPLE)
        set(VCPKG_DIR "/usr/local/vcpkg" CACHE PATH "vcpkg installation directory")
    elseif(APPLE)
        set(VCPKG_DIR "/opt/vcpkg" CACHE PATH "vcpkg installation directory")
    endif()
endif()

# -----------------------------------------------------------------------------
# [2] Qt 路径配置
# -----------------------------------------------------------------------------
if(NOT DEFINED Qt_INSTALL_DIR OR Qt_INSTALL_DIR STREQUAL "")
    if(WIN32)
        set(Qt_INSTALL_DIR "C:/Users/xx/Qt/6.11.2/msvc2022_64" CACHE PATH "Qt installation directory")
        # set(Qt_INSTALL_DIR "C:/Users/xx/Qt/6.11.2/msvc2022_64" CACHE PATH "Qt installation directory")
    elseif(UNIX AND NOT APPLE)
        set(Qt_INSTALL_DIR "/usr/local/Qt/6.11.2/gcc_64" CACHE PATH "Qt installation directory")
    elseif(APPLE)
        set(Qt_INSTALL_DIR "/Applications/Qt/6.11.1/macos" CACHE PATH "Qt installation directory")
    endif()
endif()

# -----------------------------------------------------------------------------
# [3] 模块编译开关（可通过 -D<OPTION>=ON/OFF 覆盖）
# -----------------------------------------------------------------------------
# 默认值配置（可放在 Config.local.cmake 中覆盖）
if(NOT DEFINED SANYI_DEFAULT_OPTIONAL)
    set(SANYI_DEFAULT_OPTIONAL ON)
endif()
if(NOT DEFINED SANYI_DEFAULT_CORE)
    set(SANYI_DEFAULT_CORE OFF)
endif()

# 核心模块（默认关闭）
option(BUILD_GEOMODELCORE "GeoModelCore module (OpenCASCADE-based geometry modeling)" ${SANYI_DEFAULT_CORE})

# 可选模块（默认开启）
option(BUILD_UI3D   "UI3D module (3D user interface)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_NESTING "Nesting module (2D/3D nesting algorithm)" ${SANYI_DEFAULT_OPTIONAL})

# 扩展模块（默认关闭）
option(BUILD_VISION        "Vision module (image processing/computer vision)" OFF)
option(BUILD_NETWORK       "Network module (HTTP/WebSocket/cloud sync)" OFF)
option(BUILD_HARDWARE      "Hardware module (laser control/material database)" OFF)
option(BUILD_ENGRAVING     "Engraving module (3D laser engraving)" OFF)
option(BUILD_CAM           "CAM module (laser cutting toolpath generation)" OFF)
option(BUILD_PYTHON        "Python module (PythonHost integration framework)" OFF)
option(BUILD_CRASHHANDLER  "CrashHandler module (crash capture and minidump generation)" OFF)

# =============================================================================
# 【自动配置区域】- 以下内容自动完成，无需手动修改
# =============================================================================

# 加载用户本地配置（覆盖上述默认值）
set(_SANYI_CONFIG_LOCAL "${CMAKE_CURRENT_LIST_DIR}/Config.local.cmake")
if(EXISTS "${_SANYI_CONFIG_LOCAL}")
    include("${_SANYI_CONFIG_LOCAL}")
    message(STATUS "[Config] Loaded local configuration from Config.local.cmake")
endif()

# 检查路径是否存在
if(NOT EXISTS "${VCPKG_DIR}")
    message(WARNING "VCPKG_DIR does not exist: ${VCPKG_DIR}")
endif()
if(NOT EXISTS "${Qt_INSTALL_DIR}")
    message(WARNING "Qt_INSTALL_DIR does not exist: ${Qt_INSTALL_DIR}")
endif()

# Qt 版本固定
set(QT_VERSION_MAJOR 6)

# MSVC 运行时库：动态链接 CRT
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime library" FORCE)
endif()

# Qt CMake 配置
set(Qt6_DIR "${Qt_INSTALL_DIR}/lib/cmake/Qt6" CACHE PATH "Qt CMake configuration directory")
list(INSERT CMAKE_PREFIX_PATH 0 "${Qt_INSTALL_DIR}")

# 输出配置信息
get_filename_component(QT_VERSION_FULL "${Qt_INSTALL_DIR}" PATH)
get_filename_component(QT_VERSION_FULL "${QT_VERSION_FULL}" NAME)
message(STATUS "")
message(STATUS "=== SanYi CAD Configuration ===")
message(STATUS "  Qt Version:    ${QT_VERSION_FULL}")
message(STATUS "  Qt Directory:  ${Qt_INSTALL_DIR}")
message(STATUS "  vcpkg Directory: ${VCPKG_DIR}")
message(STATUS "  Build Modules: ${SANYI_DEFAULT_OPTIONAL}/Core=${SANYI_DEFAULT_CORE}")
message(STATUS "")

# Windows 运行时环境配置
if(WIN32 AND EXISTS "${Qt_INSTALL_DIR}")
    set(_SANYI_QT_BIN "${Qt_INSTALL_DIR}/bin")
    set(_SANYI_VCPKG_BIN "${VCPKG_DIR}/installed/x64-windows/bin")
    set(ENV{PATH} "${_SANYI_QT_BIN};${_SANYI_VCPKG_BIN};$ENV{PATH}")
    set(QT_PLUGIN_PATH "${Qt_INSTALL_DIR}/plugins" CACHE PATH "Qt plugins directory")
endif()
