# =============================================================================
# AbiVisibilityGuard.cmake
#
# 配置期守卫：检查模块导出宏头文件（*API.h）在 GCC/Clang 分支上没有按
# `*_EXPORTS` 分叉，且确实给出了 visibility("default")。
#
# 为什么需要它：
#   dllimport/dllexport 是 Windows 概念；Mach-O/ELF 只有「可见性」一个维度。
#   常见的错误写法是把 Windows 的双分支照搬到 GCC/Clang：
#
#       #elif defined(__GNUC__) || defined(__clang__)
#           #ifdef FOO_EXPORTS
#               #define FOO_API __attribute__((visibility("default")))
#           #else
#               #define FOO_API                    // ← 消费侧空宏
#           #endif
#
#   配合根 CMakeLists.txt 的 -fvisibility=hidden，被标记类的 typeinfo/vtable
#   会在每个镜像里各生成一份 private extern 符号，动态链接器无法合并，
#   结果是跨 DLL 的 dynamic_cast **静默**返回 nullptr —— 编译链接全绿，
#   只在运行时表现为「某条业务链路莫名失败」。
#
#   真实事故：Engine3D 造出的 SyMeshEntity 在 UI3D 里 dynamic_cast 失败，
#   OBJ 菜单导入 100% 报 "load model failed"，而按 eType 分拣的拖拽导入正常。
#   代码审查抓不住这种问题，必须由构建系统拦。
#
# 适用范围（刻意不是全仓库）：
#   只覆盖「以 C++ 类形态跨 DLL 共享对象模型」的模块 —— 目前是
#   EngineCommon / Engine2D / Engine3D 这套 SyEntity 体系。
#   Vision、RenderX 这类**刻意**只暴露 C ABI / 纯虚接口的模块不在此列：
#   它们要的正是「符号全隐藏」，把宏改成一律 default 反而破坏其设计。
#   新模块若开始跨 DLL 传递 C++ 对象并做 downcast，再往调用处的列表里加。
#
# 输出消息一律英文：这是构建日志，会进 CI 与现场排查记录。
# =============================================================================

function(sanyi_check_export_macro_visibility)
    set(options "")
    set(oneValueArgs "")
    set(multiValueArgs HEADERS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_HEADERS)
        message(FATAL_ERROR "sanyi_check_export_macro_visibility: HEADERS is required")
    endif()

    set(_violations "")
    set(_checked 0)

    foreach(_header IN LISTS ARG_HEADERS)
        if(NOT EXISTS "${_header}")
            # 头文件被改名/移走也算违规：守卫悄悄失效比没有守卫更糟
            list(APPEND _violations "  ${_header}: header not found, guard list is stale")
            continue()
        endif()

        file(STRINGS "${_header}" _lines ENCODING UTF-8)

        set(_seen_gnuc FALSE)
        set(_has_default_visibility FALSE)
        set(_forks_on_exports FALSE)

        foreach(_line IN LISTS _lines)
            # 先剥行注释：本文件与各 API.h 的注释里都会提到 EXPORTS / visibility，
            # 直接对原文匹配会把说明文字本身判成违规
            string(REGEX REPLACE "//.*$" "" _code "${_line}")

            if(NOT _seen_gnuc)
                if(_code MATCHES "__GNUC__|__clang__")
                    set(_seen_gnuc TRUE)
                endif()
                continue()
            endif()

            # GNUC/Clang 分支之后不允许再出现 *_EXPORTS 条件编译
            if(_code MATCHES "_EXPORTS")
                set(_forks_on_exports TRUE)
            endif()
            if(_code MATCHES "visibility\\(\"default\"\\)")
                set(_has_default_visibility TRUE)
            endif()
        endforeach()

        get_filename_component(_name "${_header}" NAME)

        if(NOT _seen_gnuc)
            list(APPEND _violations
                "  ${_name}: no GCC/Clang branch found, non-Windows builds get no visibility attribute")
        else()
            if(_forks_on_exports)
                list(APPEND _violations
                    "  ${_name}: the GCC/Clang branch still forks on *_EXPORTS -- the consumer side gets an empty macro, so typeinfo is not shared and cross-DLL dynamic_cast returns nullptr")
            endif()
            if(NOT _has_default_visibility)
                list(APPEND _violations
                    "  ${_name}: the GCC/Clang branch does not define visibility(\"default\")")
            endif()
        endif()

        math(EXPR _checked "${_checked} + 1")
    endforeach()

    if(_violations)
        string(REPLACE ";" "\n" _violation_text "${_violations}")
        message(FATAL_ERROR
            "[ABI visibility guard] export macro headers break the shared-RTTI contract:\n"
            "${_violation_text}\n"
            "On Mach-O/ELF the import and export sides must both expand to "
            "__attribute__((visibility(\"default\"))) -- dllimport has no counterpart there.\n"
            "See CMake/AbiVisibilityGuard.cmake for the incident this rule comes from.\n")
    endif()

    message(STATUS "  ABI visibility guard: PASSED (${_checked} export macro header(s) checked)")
endfunction()
