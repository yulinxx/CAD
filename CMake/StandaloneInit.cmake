# StandaloneInit.cmake
# 单模块独立编译入口（Module/CMakeLists.txt）在 project() 之前 include。
# 期望调用方已 include Config.cmake。

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

if(DEFINED VCPKG_DIR AND EXISTS "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${VCPKG_DIR}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
    message(STATUS "[Standalone] VCPKG: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(WARNING "[Standalone] VCPKG_DIR not set or invalid; check Config.cmake")
endif()

get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SANYI_ROOT "${SANYI_ROOT}" CACHE INTERNAL "SanYi CAD project root")
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 延迟设置编译选项，直到 project() 之后
# MSVC 变量在 project() 调用之后才会被正确设置
macro(sanyi_setup_compiler_options)
    if(MSVC)
        add_compile_options(/W4 /utf-8)
        add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
        add_compile_options(/wd4251 /wd4244)
    else()
        add_compile_options(-Wall -Wextra -Wpedantic)
        add_compile_options(-Wno-conversion -Wno-float-conversion)
    endif()
endmacro()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

set(_SANYI_BIN_DIR "${CMAKE_BINARY_DIR}/bin_Qt${QT_VERSION_MAJOR}")
set(_SANYI_LIB_DIR "${CMAKE_BINARY_DIR}/lib_Qt${QT_VERSION_MAJOR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_SANYI_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_SANYI_LIB_DIR}")

foreach(_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_cfg}" _cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_cfg_u} "${_SANYI_BIN_DIR}/${_cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_cfg_u} "${_SANYI_BIN_DIR}/${_cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_cfg_u} "${_SANYI_LIB_DIR}/${_cfg}")
endforeach()

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

macro(sanyi_find_gtest)
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        include(GoogleTest)
        enable_testing()
        message(STATUS "[Standalone] Google Test: found")
    else()
        message(STATUS "[Standalone] Google Test: not found, tests disabled")
    endif()
endmacro()

message(STATUS "[Standalone] Root: ${SANYI_ROOT}")
message(STATUS "[Standalone] Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------")
