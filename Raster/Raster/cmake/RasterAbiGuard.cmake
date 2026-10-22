# =============================================================================
# RasterAbiGuard.cmake
#
# 配置期 ABI 守卫：扫描 Include/Raster 下的**公开头文件**，
# 一旦出现违反跨 DLL 约定的构造就直接 FATAL_ERROR，构建停在配置阶段。
#
# 为什么必须有这道闸：
#   Raster 的产品定位是"导出为 DLL，供其它工程（含第三方客户程序）调用"。
#   这个承诺成立的唯一条件是——公开头里不出现 STL / OpenCV / 异常 / 动态类型。
#   而这类违规**加进去时不会报任何错**：本机编译通过、单测通过，
#   直到某个客户用不同编译器 / 不同 CRT / 不同 _ITERATOR_DEBUG_LEVEL 构建
#   消费程序时才炸，而且现场表现是"在别的模块里 free() 时崩溃"，几乎无法回溯。
#
#   本仓库的旧模块就是这么烂掉的：公开头里直接写 cv::Mat / std::string，
#   最后只能在头注释里写"不承诺跨 DLL ABI"来承认失败。
#   所以这条规则必须由构建系统强制，不能靠 code review 记性。
#
# 两条规则集：
#   1) C ABI 头（默认全部头，含 RasterCpp.h 之外的）：
#      禁止 std::* / cv::* / OpenCV 头 / Qt / 异常 / 动态_cast / 内联变量等。
#   2) RasterCpp.h（C++ 门面，允许用 STL）：
#      但**禁止出现 RASTER_API / RASTER_EXPORT**——
#      它必须是纯 header-only inline，一旦导出带 std::string 的符号，
#      就等于把"STD 导出问题"重新引进来。
#
# 注意：必须先剥注释再匹配。头文件的注释里大量出现
# "不要用 std::string / 禁用 dynamic_cast" 这类说明文字，
# 直接对原文匹配会把说明本身报成违规。
#
# 输出消息一律英文：这是构建日志，会进 CI 与现场排查记录。
# =============================================================================

# 剥掉 C/C++ 注释，把结果写回 out_var。
# 逐行处理并维护块注释状态；对单行内的 /* ... */ 直接贪婪删除。
# 够用即可：头文件里不会出现"字符串字面量里含 //"这类刁钻情况。
function(_raster_strip_comments in_text out_var)
    # 先把 ; 转义，否则 CMake 会把它当列表分隔符把行切碎
    string(REPLACE ";" "\\;" _escaped "${in_text}")
    string(REGEX REPLACE "\r?\n" ";" _lines "${_escaped}")

    set(_result "")
    set(_in_block FALSE)

    foreach(_line IN LISTS _lines)
        if(_in_block)
            if(_line MATCHES "\\*/")
                # 块注释在本行结束，只保留 */ 之后的部分
                string(REGEX REPLACE "^.*\\*/" "" _line "${_line}")
                set(_in_block FALSE)
            else()
                continue()
            endif()
        endif()

        # 单行内成对的 /* ... */
        string(REGEX REPLACE "/\\*.*\\*/" "" _line "${_line}")

        # 块注释在本行开始且未结束
        if(_line MATCHES "/\\*")
            string(REGEX REPLACE "/\\*.*$" "" _line "${_line}")
            set(_in_block TRUE)
        endif()

        # 行注释
        string(REGEX REPLACE "//.*$" "" _line "${_line}")

        string(APPEND _result "${_line}\n")
    endforeach()

    set(${out_var} "${_result}" PARENT_SCOPE)
endfunction()

function(sanyi_raster_abi_guard)
    set(options "")
    set(oneValueArgs INCLUDE_DIR)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT ARG_INCLUDE_DIR)
        message(FATAL_ERROR "sanyi_raster_abi_guard: INCLUDE_DIR is required")
    endif()

    file(GLOB_RECURSE _raster_public_headers "${ARG_INCLUDE_DIR}/*.h" "${ARG_INCLUDE_DIR}/*.hpp")

    # C++ 门面头：允许 STL，但必须保持 header-only inline。
    set(_cpp_facade_headers)
    set(_c_abi_headers)
    foreach(_header IN LISTS _raster_public_headers)
        file(RELATIVE_PATH _rel "${ARG_INCLUDE_DIR}" "${_header}")
        if(_rel MATCHES "RasterCpp\\.h$")
            list(APPEND _cpp_facade_headers "${_header}")
        else()
            list(APPEND _c_abi_headers "${_header}")
        endif()
    endforeach()

    # 每条规则是「正则 → 人读原因」，原因写英文（构建日志）。
    # 注意：原因文本里**不能出现分号**——CMake 会把它当列表分隔符，
    # 导致后面的 list(GET) 索引全部错位。
    set(_rules
        "#[ \t]*include[ \t]*[<\"]opencv2"  "OpenCV headers must stay in Src, consumers must not need OpenCV headers"
        "cv::"                              "OpenCV types must not appear in public headers"
        "std::string"                       "std::string layout depends on toolchain and CRT, use a fixed char[N] plus a size argument"
        "std::vector"                       "std::vector must not cross a DLL boundary, use pointer plus count"
        "std::array"                        "std::array is fine as a value type but it is not needed here, keep public headers minimal"
        "std::map"                          "std::map must not cross a DLL boundary, use a fixed table looked up by id"
        "std::set"                          "std::set must not cross a DLL boundary, use a flat array"
        "std::function"                     "std::function layout is implementation defined, use a plain function pointer"
        "std::shared_ptr"                   "shared_ptr control blocks cannot be shared across DLLs, use explicit ownership"
        "std::unique_ptr"                   "unique_ptr deleter is part of the type, keep ownership inside the library"
        "std::optional"                     "std::optional is not trivially copyable across ABI boundaries, use an explicit flag plus value"
        "dynamic_cast"                      "typeinfo is not shared across DLLs under hidden visibility, keep the API C-only"
        "#[ \t]*include[ \t]*<Q"            "Raster must not depend on Qt, the host converts events into Qt signals"
        "QString"                           "Raster must not depend on Qt types"
        "QObject"                           "Raster must not depend on Qt types"
        "[ \t]throw[ \t(]"                  "exceptions must not cross a DLL boundary, return RasterStatus instead"
    )

    set(_violations "")

    foreach(_header IN LISTS _c_abi_headers)
        file(READ "${_header}" _raw)
        _raster_strip_comments("${_raw}" _code)
        file(RELATIVE_PATH _rel "${ARG_INCLUDE_DIR}" "${_header}")

        set(_index 0)
        list(LENGTH _rules _rule_count)
        while(_index LESS _rule_count)
            list(GET _rules ${_index} _pattern)
            math(EXPR _reason_index "${_index} + 1")
            list(GET _rules ${_reason_index} _reason)

            if(_code MATCHES "${_pattern}")
                list(APPEND _violations "  ${_rel}: '${_pattern}' -- ${_reason}")
            endif()

            math(EXPR _index "${_index} + 2")
        endwhile()
    endforeach()

    # C++ 门面必须保持 header-only：一旦出现导出宏，
    # 就意味着有带 STL 的符号被编进 DLL，STD 导出问题会重新出现。
    foreach(_header IN LISTS _cpp_facade_headers)
        file(READ "${_header}" _raw)
        _raster_strip_comments("${_raw}" _code)
        file(RELATIVE_PATH _rel "${ARG_INCLUDE_DIR}" "${_header}")
        if(_code MATCHES "RASTER_API" OR _code MATCHES "RASTER_EXPORT")
            list(APPEND _violations
                "  ${_rel}: uses RASTER_API -- the C++ facade must stay header-only inline, never export an STL bearing symbol")
        endif()
    endforeach()

    list(LENGTH _c_abi_headers _abi_header_count)

    if(_violations)
        string(REPLACE ";" "\n" _violation_text "${_violations}")
        message(FATAL_ERROR
            "[Raster ABI guard] public headers break the cross-DLL contract:\n"
            "${_violation_text}\n"
            "Public headers may only use POD types, fixed arrays, pointer+count, enums and opaque handles.\n"
            "See Docs/09-Raster/Raster module architecture, section 2.\n"
            "STL in public headers is not a style issue: it breaks at runtime when the caller and the\n"
            "library disagree on _ITERATOR_DEBUG_LEVEL or CRT, and the crash lands in another module.\n"
            "To bypass temporarily configure with -DRASTER_ABI_GUARD=OFF, but do not commit that setting.\n")
    endif()

    message(STATUS "  ABI guard: PASSED (${_abi_header_count} C ABI headers, "
                   "${_cpp_facade_headers} header-only C++ facade headers checked)")
endfunction()
