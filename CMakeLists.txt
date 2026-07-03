cmake_minimum_required(VERSION 3.20)

# ============ CMake 策略设置 ============
# CMP0167: FindBoost 模块从 CMake 3.25 开始已被移除，必须使用 CONFIG 模式
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# 包含项配置文件
include(Config.txt)

# 集成VCPKG
set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
# 禁用 vcpkg 自动部署 DLL
# set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Disable automatic DLL deployment")

# 项目基本信息
project(SanYiCAD
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD System"
    LANGUAGES CXX
)

set(SANYI_ROOT "${PROJECT_SOURCE_DIR}" CACHE INTERNAL "SanYi CAD project root")
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")
include("${SANYI_ROOT}/CMake/Utils.cmake")

# 禁用 Qt6 查找 WrapVulkanHeaders 的警告 
set(Vulkan_INCLUDE_DIR "" CACHE INTERNAL "Suppress WrapVulkanHeaders warning")

message(STATUS "")
message(STATUS "==========================================")
message(STATUS "  SanYi CAD System Configuration")
message(STATUS "==========================================")
message(STATUS "")
message(STATUS "  Project: ${PROJECT_NAME}")
message(STATUS "  VCPKG_DIR: ${VCPKG_DIR}")

find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
    message(STATUS "  Git: Updating submodules...")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        RESULT_VARIABLE GIT_SUBMOD_RESULT
    )
    if(NOT GIT_SUBMOD_RESULT EQUAL "0")
        message(WARNING "  Git: Failed to update submodules!")
        message(WARNING "  Please run manually:")
        message(WARNING "    cd ${CMAKE_CURRENT_SOURCE_DIR}")
        message(WARNING "    git submodule update --init --recursive")
    else()
        message(STATUS "  Git: Submodules updated successfully")
    endif()
else()
    message(STATUS "  Git: Not found or not a git repository")
endif()

# 统一 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 符号可见性控制：默认隐藏，仅通过宏导出必要符号
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# 统一 MSVC 运行时库设置
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    add_compile_options("/utf-8")
endif()

# 统一输出目录
set(_SANYI_BIN_DIR "${CMAKE_BINARY_DIR}/bin_Qt${QT_VERSION_MAJOR}")
set(_SANYI_LIB_DIR "${CMAKE_BINARY_DIR}/lib_Qt${QT_VERSION_MAJOR}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_SANYI_LIB_DIR}")

foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${cfg}" cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_u} "${_SANYI_BIN_DIR}/${cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_u} "${_SANYI_BIN_DIR}/${cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_u} "${_SANYI_LIB_DIR}/${cfg}")
endforeach()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

find_package(GTest CONFIG QUIET)
if(GTest_FOUND)
    include(GoogleTest)
    message(STATUS "  Google Test: Found, unit tests enabled")
    
    option(BUILD_ALL_TESTS "Build all unit tests" OFF)
    option(BUILD_UTILITY_TESTS "Build Utility unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_LOG_TESTS "Build Log unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_ENGINE2D_TESTS "Build Engine2D unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_ENGINE3D_TESTS "Build Engine3D unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_NESTING_TESTS "Build Nesting unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_GEOMODELCORE_TESTS "Build GeoModelCore unit tests" ${BUILD_ALL_TESTS})
    option(BUILD_FILEIO_TESTS "Build FileIO unit tests" ${BUILD_ALL_TESTS})
    if(BUILD_ENGRAVING)
        option(BUILD_ENGRAVING_TESTS "Build Engraving unit tests" ON)
    else()
        option(BUILD_ENGRAVING_TESTS "Build Engraving unit tests" OFF)
    endif()
    
    enable_testing()
else()
    message(STATUS "  Google Test: Not found, unit tests disabled")
    set(BUILD_UTILITY_TESTS OFF CACHE BOOL "Build Utility unit tests" FORCE)
    set(BUILD_LOG_TESTS OFF CACHE BOOL "Build Log unit tests" FORCE)
    set(BUILD_ENGINE2D_TESTS OFF CACHE BOOL "Build Engine2D unit tests" FORCE)
    set(BUILD_ENGINE3D_TESTS OFF CACHE BOOL "Build Engine3D unit tests" FORCE)
    set(BUILD_GEOMODELCORE_TESTS OFF CACHE BOOL "Build GeoModelCore unit tests" FORCE)
    set(BUILD_NESTING_TESTS OFF CACHE BOOL "Build Nesting unit tests" FORCE)
    set(BUILD_FILEIO_TESTS OFF CACHE BOOL "Build FileIO unit tests" FORCE)
    set(BUILD_ENGRAVING_TESTS OFF CACHE BOOL "Build Engraving unit tests" FORCE)
endif()

message(STATUS "")
message(STATUS "***************************************************************")

# 1. Core 层 - 共享基础设施
add_subdirectory(Utility/Utility)

# 1.5 CrashHandler 层 - 崩溃捕获（在日志之前，确保初始化阶段崩溃也能捕获）
add_subdirectory(CrashHandler/CrashHandler)

# 2. Log 层 - 日志库（Engine 层依赖）
add_subdirectory(Log/Log)

# 3. Engine 层 - 引擎核心
add_subdirectory(Engine/Common)
add_subdirectory(Engine/2D)
add_subdirectory(Engine/3D)

# 4. Render 层 - 渲染
add_subdirectory(Render/Common)
add_subdirectory(Render/2D)
add_subdirectory(Render/3D)

# 4.5 独立渲染 DLL（纯 POD 契约，不依赖 Engine/Ut）
option(BUILD_SANYI_RENDER "Build standalone SanYiRender DLL" ON)
if(BUILD_SANYI_RENDER)
    add_subdirectory(SanYiRender)
endif()

# 4.6 GeoModelCore（须在 UI3D 之前，以便 UI3D 链接并继承头文件路径）
if(BUILD_GEOMODELCORE)
    find_package(OpenCASCADE CONFIG REQUIRED)
    add_subdirectory(GeoModelCore/GeoModelCore)
    message(STATUS "  GeoModelCore: ENABLED")
else()
    message(STATUS "  GeoModelCore: DISABLED")
endif()

# 4.7 Engraving（须在 UI 之前，以便 UI2D/UI3D 链接）
if(BUILD_ENGRAVING)
    add_subdirectory(Engraving/Engraving)
    message(STATUS "  Engraving: ENABLED")
else()
    message(STATUS "  Engraving: DISABLED")
endif()

# 5. UI 层 - 用户界面
add_subdirectory(UI/Common)
add_subdirectory(UI/2D)
add_subdirectory(UI/3D)

# 6. 功能模块
add_subdirectory(FileIO/FileIO)
add_subdirectory(Nesting/Nesting)

message(STATUS "----------------------------------------")
if(BUILD_HARDWARE)
    add_subdirectory(Hardware/Hardware)
    message(STATUS "  Hardware: ENABLED")
else()
    message(STATUS "  Hardware: DISABLED")
endif()

if(BUILD_NETWORK)
    add_subdirectory(Network/Network)
    message(STATUS "  Network: ENABLED")
else()
    message(STATUS "  Network: DISABLED")
endif()

if(BUILD_VISION)
    add_subdirectory(Vision/Vision)
    message(STATUS "  Vision: ENABLED")
else()
    message(STATUS "  Vision: DISABLED")
endif()

message(STATUS "----------------------------------------")

# 7. Python 集成
option(SANYI_PYTHON_HOST "Enable PythonHost integration framework" ON)
option(SANYI_PYTHON_EMBED "Enable in-process Python bridge (Phase 3)" OFF)
set(SANYI_PYTHON_VERSION "3.11" CACHE STRING "Required Python version")

if(SANYI_PYTHON_HOST)
    add_subdirectory(PythonHost)
    message(STATUS "  PythonHost: ENABLED")
else()
    message(STATUS "  PythonHost: DISABLED")
endif()

option(SANYI_PYBIND_CORE "Build PyBindCore facade extension (_sanyi_core)" ON)
if(SANYI_PYBIND_CORE AND SANYI_PYTHON_HOST)
    add_subdirectory(PyBindCore)
    message(STATUS "  PyBindCore: ENABLED")
else()
    message(STATUS "  PyBindCore: DISABLED")
endif()

# 8. 应用入口
add_subdirectory(Main)