# StandaloneInit.cmake
# 单模块独立编译入口（Module/CMakeLists.txt）在 project() 之前 include。
# 期望调用方已 include Config.cmake。
#
# 注意：工具链、输出目录、C++ 标准、构建类型等**全部由 Config.cmake 定义**，
# 本文件不再重复设置，只补独立构建特有的部分（CMP 策略、APR_ROOT、编译选项宏）。

if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

get_filename_component(APR_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(APR_ROOT "${APR_ROOT}" CACHE INTERNAL "April CAD project root")
include("${APR_ROOT}/CMake/AprPaths.cmake")

# 延迟设置编译选项，直到 project() 之后
# MSVC 变量在 project() 调用之后才会被正确设置
macro(apr_setup_compiler_options)
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

    # 与根构建的 APR_WERROR 语义保持一致（独立模块无 ThirdParty 源码）。
    if(APR_WERROR)
        if(MSVC)
            add_compile_options(/WX)
        else()
            add_compile_options(-Werror)
        endif()
    endif()

    # 与根构建的 APR_SANITIZE 语义保持一致，供独立模块在手动 sanitizer 作业里复用。
    if(APR_SANITIZE)
        if(MSVC)
            message(FATAL_ERROR
                "[Standalone] APR_SANITIZE is not supported with MSVC.")
        endif()
        message(STATUS "[Standalone] Sanitizers: ${APR_SANITIZE}")
        add_compile_options("-fsanitize=${APR_SANITIZE}" -fno-omit-frame-pointer -g)
        add_link_options("-fsanitize=${APR_SANITIZE}")
    endif()
endmacro()

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# 兄弟模块拉入（仅独立构建入口使用）：
#   apr_add_sibling(<target> <module_dir> [OPTIONAL])
# target 已存在（集成构建）则跳过；源码目录缺失时默认 FATAL_ERROR，
# 传 OPTIONAL 则降级为 WARNING —— 只用于日志这类可观测性依赖。
# 被依赖者必须先于依赖者拉入，否则 INTERFACE 包含目录拿不到，
# configure 不报错，编译阶段才以 "cannot open include file" 暴露。
function(apr_add_sibling _tgt _dir)
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

# 独立构建只关心本模块自己的测试：声明本构建树允许构建测试的模块键。
# 用法：apr_only_own_tests(<模块键> [<模块键2> ...])
# 模块键取自 Config.cmake 的 APR_TEST_MODULES，例如
#   apr_only_own_tests(ENGINE2D ENGINE3D)
# 空参数表示「本树任何模块都不构建测试」，PyBindCore/CMakeLists.txt 依赖该语义，
# 因此这里必须显式写入空字符串，不能靠「未定义」来表达。
# 必须在 apr_add_sibling() 与任何内层 add_subdirectory() 之前调用：
# 内层模块通过 apr_module_tests_enabled(<键>) 读取本变量决定是否挂测试。
macro(apr_only_own_tests)
    # 护栏：只有模块自身作为顶层工程（独立构建）时才声明测试范围。
    # 集成构建里模块只是根工程子目录，测试范围由根 CMakeLists.txt 统一管理，
    # 在这里声明会把兄弟模块（含 Main）的测试静默关掉。
    # 注意：宏体内的 return() 只结束本宏的展开，不会中断调用方 CMakeLists 的解析。
    if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        return()
    endif()
    # 注意：宏内的 foreach(IN LISTS ARGN) 拿不到参数（CMake 宏的 ARGN 只能经
    # ${ARGN} 展开），这里整体落到普通变量，允许为空。
    set(APR_STANDALONE_OWN_MODULES "${ARGN}")
    message(STATUS "[Standalone] Test modules: ${APR_STANDALONE_OWN_MODULES}")
endmacro()

# GTest 探测：不可用时把唯一总开关 BUILD_TESTS 降级为 OFF。
# 这是独立构建下的唯一降级点（集成构建由根 CMakeLists.txt 承担同一职责）。
macro(apr_find_gtest)
    # 护栏：同 apr_only_own_tests —— 集成构建的 GTest 探测与降级由根
    # CMakeLists.txt 负责，子目录里执行会把全局测试开关改写掉。
    # 宏内的 return() 只退出本宏。
    if(NOT CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        return()
    endif()
    find_package(GTest CONFIG QUIET)
    if(GTest_FOUND)
        include(GoogleTest)
        enable_testing()
        message(STATUS "[Standalone] Google Test: found")
    else()
        message(STATUS "[Standalone] Google Test: not found, tests disabled")
        apr_force_tests_off("GTest not found")
    endif()
endmacro()

# 独立构建入口统一收口：先按需探测 GTest 并完成降级，再打印最终测试状态。
# 所有独立入口都只调用它一次，状态输出逻辑集中在 Config.cmake 的
# apr_print_test_status()，避免每个入口各写一份。
macro(apr_finalize_tests)
    if(BUILD_TESTS)
        apr_find_gtest()
    endif()
    apr_print_test_status()
endmacro()

message(STATUS "[Standalone] Root: ${APR_ROOT}")
message(STATUS "[Standalone] Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "----------------------------------------")
