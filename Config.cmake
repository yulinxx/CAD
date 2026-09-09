# ============================================================================
# Config.cmake — 全局构建配置
# ============================================================================

# --------------------------------------------------------------------
# vcpkg 配置
# --------------------------------------------------------------------
if(NOT DEFINED VCPKG_DIR OR VCPKG_DIR STREQUAL "")
    if(WIN32)
        set(VCPKG_DIR "C:/vcpkg/" CACHE PATH "VCPKG installation directory")
    elseif(UNIX AND NOT APPLE)
        set(VCPKG_DIR "/usr/local/vcpkg/" CACHE PATH "VCPKG installation directory")
    elseif(APPLE)
        set(VCPKG_DIR "/Users/ms/vcpkg" CACHE PATH "VCPKG installation directory")
    endif()
endif()

# --------------------------------------------------------------------
# Qt 版本配置（已固定 Qt6）
# --------------------------------------------------------------------
set(QT_VERSION_MAJOR 6)

# MSVC 运行库设置：动态链接 CRT
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" CACHE STRING "MSVC runtime library" FORCE)
endif()

# --------------------------------------------------------------------
# Qt 路径配置
# --------------------------------------------------------------------
if(NOT DEFINED Qt_INSTALL_DIR OR Qt_INSTALL_DIR STREQUAL "")
    if(WIN32)
        set(Qt_INSTALL_DIR "C:/Qt/6.11.1/msvc2022_64" CACHE PATH "Qt installation directory")
    elseif(UNIX AND NOT APPLE)
        set(Qt_INSTALL_DIR "/usr/local/Qt/6.11.1/gcc_64" CACHE PATH "Qt installation directory")
    elseif(APPLE)
        set(Qt_INSTALL_DIR "/Users/ms/Qt/6.11.1/macos" CACHE PATH "Qt installation directory")
    endif()
endif()

# --------------------------------------------------------------------
# 模块编译开关配置
# --------------------------------------------------------------------
if(NOT DEFINED SANYI_DEFAULT_OPTIONAL)
    set(SANYI_DEFAULT_OPTIONAL ON)
endif()
if(NOT DEFINED SANYI_DEFAULT_CORE)
    set(SANYI_DEFAULT_CORE OFF)
endif()

option(BUILD_VISION "Build Vision module (image processing, computer vision)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_NETWORK "Build Network module (HTTP, WebSocket, cloud sync)" OFF)
option(BUILD_HARDWARE "Build Hardware module (laser control, material database)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_ENGRAVING "Build Engraving module (3D laser engraving)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_GEOMODELCORE "Build GeoModelCore module (OpenCASCADE-based geometry modeling)" ${SANYI_DEFAULT_CORE})
option(BUILD_UI3D "Build UI3D module (3D user interface)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_NESTING "Build Nesting module (2D/3D nesting/arrangement algorithm)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_CAM "Build CAM module (laser cutting toolpath generation)" ${SANYI_DEFAULT_OPTIONAL})
option(BUILD_CRASHHANDLER "Build CrashHandler module (crash capture and reporting)" ON)
option(BUILD_PYTHON "Build Python module (PythonHost integration framework)" OFF)

# ====================================================================
# 自动配置区域
# ====================================================================

set(Qt6_DIR "${Qt_INSTALL_DIR}/lib/cmake/Qt6" CACHE PATH "Qt CMake configuration directory")

list(INSERT CMAKE_PREFIX_PATH 0 "${Qt_INSTALL_DIR}")

message(STATUS "[Qt]")
message(STATUS "  Install Directory: ${Qt_INSTALL_DIR}")
message(STATUS "  Config Directory: ${Qt6_DIR}")
message(STATUS "[vcpkg]")
message(STATUS "  VCPKG_DIR: ${VCPKG_DIR}")

if(EXISTS "${Qt_INSTALL_DIR}" AND WIN32)
    set(_SANYI_QT_BIN "${Qt_INSTALL_DIR}/bin")
    set(_SANYI_VCPKG_BIN "${VCPKG_DIR}/installed/x64-windows/bin")
    set(ENV{PATH} "${_SANYI_QT_BIN};${_SANYI_VCPKG_BIN};$ENV{PATH}")
    set(QT_PLUGIN_PATH "${Qt_INSTALL_DIR}/plugins" CACHE PATH "Qt plugins directory")
endif()
