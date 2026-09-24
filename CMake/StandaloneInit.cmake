# StandaloneInit.cmake
# 单模块独立编译入口（Module/CMakeLists.txt）在 project() 之前 include。
# 期望调用方已 include Config.cmake。
#
# 注意：工具链、输出目录、C++ 标准、构建类型等**全部由 Config.cmake 定义**，
# 本文件不再重复设置，只补独立构建特有的部分（CMP 策略、SANYI_ROOT、编译选项宏）。

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

get_filename_component(SANYI_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(SANYI_ROOT "${SANYI_ROOT}" CACHE INTERNAL "SanYi CAD project root")
include("${SANYI_ROOT}/CMake/SanYiPaths.cmake")

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
        add_compile_options(
            -Wno-unused-parameter
            -Wno-unused-variable
            -Wno-unused-private-field
            -Wno-unused-lambda-capture
            -Wno-nullability-extension
            -Wno-reorder
        )
        if(APPLE)
            add_compile_options(-Wno-nonportable-include-path)
            add_compile_options(-Wno-deprecated-declarations)
        endif()
    endif()
endmacro()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# GTest 探测：不可用时把 Config.cmake 定义的全部测试开关统一降级为 OFF。
# 这是独立构建下的唯一降级点（集成构建由根 CMakeLists.txt 承担同一职责）。
macro(sanyi_find_gtest)
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        include(GoogleTest)
        enable_testing()
        message(STATUS "[Standalone] Google Test: found")
    else()
        message(STATUS "[Standalone] Google Test: not found, tests disabled")
        foreach(_sanyi_test IN LISTS SANYI_TEST_SWITCHES)
            set(${_sanyi_test} OFF CACHE BOOL "disabled: GTest not found" FORCE)
        endforeach()
    endif()
endmacro()

message(STATUS "[Standalone] Root: ${SANYI_ROOT}")
message(STATUS "[Standalone] Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------")
