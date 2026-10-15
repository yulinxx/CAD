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

# 兄弟模块拉入（仅独立构建入口使用）：
#   sanyi_add_sibling(<target> <module_dir> [OPTIONAL])
# target 已存在（集成构建）则跳过；源码目录缺失时默认 FATAL_ERROR，
# 传 OPTIONAL 则降级为 WARNING —— 只用于日志这类可观测性依赖。
# 被依赖者必须先于依赖者拉入，否则 INTERFACE 包含目录拿不到，
# configure 不报错，编译阶段才以 "cannot open include file" 暴露。
function(sanyi_add_sibling _tgt _dir)
    if(TARGET ${_tgt})
        return()
    endif()
    if(EXISTS "${_dir}/CMakeLists.txt")
        message(STATUS "[Standalone] Sibling ${_tgt}: building from ${_dir}")
        add_subdirectory("${_dir}" "${CMAKE_BINARY_DIR}/${_tgt}")
    else()
        cmake_parse_arguments(_sib "OPTIONAL" "" "" ${ARGN})
        if(_sib_OPTIONAL)
            message(WARNING
                "[Standalone] Optional sibling ${_tgt} not found at ${_dir}; "
                "continuing without it")
        else()
            message(FATAL_ERROR
                "[Standalone] Required sibling ${_tgt} not found at ${_dir}. "
                "This module cannot be built standalone without it.")
        endif()
    endif()
endfunction()

# 独立构建只关心本模块自己的测试：把兄弟模块的测试开关统一关闭。
# 支持多开关（多子库模块如 Engine: BUILD_ENGINE2D_TESTS BUILD_ENGINE3D_TESTS）：
#   sanyi_only_own_tests(<own_switch> [<own_switch2> ...])
# 必须在 sanyi_add_sibling() 之前调用（Config.cmake 每次 configure 都会把
# 测试开关 FORCE 为 ON，本宏在其后改写，下一回合仍生效）。
macro(sanyi_only_own_tests)
    # 注意：宏内的 foreach(IN LISTS ARGN) 拿不到参数（CMake 宏的 ARGN 只能经
    # ${ARGN} 展开），必须先落到普通变量再遍历，否则会把本模块的开关也关掉。
    set(_sanyi_own_switches ${ARGN})
    foreach(_sanyi_sib_tests IN LISTS SANYI_TEST_SWITCHES)
        list(FIND _sanyi_own_switches "${_sanyi_sib_tests}" _sanyi_idx)
        if(_sanyi_idx EQUAL -1)
            set(${_sanyi_sib_tests} OFF CACHE BOOL "standalone: sibling tests off" FORCE)
        endif()
    endforeach()
    unset(_sanyi_own_switches)
    unset(_sanyi_idx)
endmacro()

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
