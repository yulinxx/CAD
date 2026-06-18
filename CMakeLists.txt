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
project(CAD
    VERSION 1.0.0
    DESCRIPTION "SanYi CAD System - Unified 2D/3D"
    LANGUAGES CXX
)

include("${PROJECT_SOURCE_DIR}/CMake/Utils.cmake")

message(STATUS "")
message(STATUS "==========================================")
message(STATUS "  SanYi CAD System Configuration")
message(STATUS "==========================================")
message(STATUS "")
message(STATUS "[System]")
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

# MSVC 编译选项：使用 UTF-8 编码，避免 C4819 警告
if(MSVC)
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
else()
    message(STATUS "  Google Test: Not found, unit tests disabled")
endif()

message(STATUS "")
message(STATUS "***************************************************************")

# 1. Core 层 - 共享基础设施
add_subdirectory(Core/Utility)

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

# 5. UI 层 - 用户界面
add_subdirectory(UI/Common)
add_subdirectory(UI/2D)
add_subdirectory(UI/3D)
add_subdirectory(UI/App)

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

if(BUILD_ENGRAVING)
    add_subdirectory(Engraving/Engraving)
    message(STATUS "  Engraving: ENABLED")
else()
    message(STATUS "  Engraving: DISABLED")
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

if(BUILD_GEOMODELCORE)
    find_package(OpenCASCADE REQUIRED)
    add_subdirectory(GeoModelCore/GeoModelCore)
    message(STATUS "  GeoModelCore: ENABLED")
else()
    message(STATUS "  GeoModelCore: DISABLED")
endif()
message(STATUS "----------------------------------------")

# 7. Python 扩展
option(BUILD_PYEXTEND "Build Python extension module" ON)
if(BUILD_PYEXTEND)
    add_subdirectory(PyExtend)
endif()

# 8. 应用入口
add_subdirectory(Main)