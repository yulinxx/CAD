# 分层依赖自动检查：越界 include 即失败，作为 ctest 一部分运行。
# 规则与 README 中的依赖表保持一致。
# 使用：cmake -DUI_SOURCE_DIR=<root> -P cmake/CheckLayers.cmake

# CMP0219：宏参数中反斜杠的处理方式。NEW 策略保留原始反斜杠，
# 这对正则表达式至关重要（\. 不应被解释为转义序列）。
# 该策略在 CMake 4.4 引入，项目最低版本为 4.3，需按可用性启用。
if(POLICY CMP0219)
    cmake_policy(SET CMP0219 NEW)
endif()

if(NOT UI_SOURCE_DIR)
    message(FATAL_ERROR "UI_SOURCE_DIR is not set")
endif()

set(_violations "")

# ============================================================
# 公共变量：统一层目录名与引用前缀，避免硬编码。
# ============================================================

# 公共头根目录（include 路径第一级）。
set(_pub_root "include/UiShell")

# 各层目录名：Contract 是所有层共享的最底层。
set(_layer_contract   "Contract")
set(_layer_model      "Model")
set(_layer_assembly   "Assembly")
set(_layer_titlebar   "TitleBar")
set(_layer_platform   "Platform")
set(_layer_i18n       "i18n")

# 完整层名列表（用于组装禁止正则）。
set(_all_layers "${_layer_contract}|${_layer_model}|${_layer_assembly}"
                "|${_layer_titlebar}|${_layer_platform}"
                "|${_layer_i18n}")

# 引用前缀：匹配三种形式——公共头 "UiShell/<层>/xxx.h"、内部头 "../<层>/xxx.h"、
# src 根相对写法 "<层>/xxx.h"。
set(_ref "(UiShell/|\\.\\./|#include[ \t]*\")")

# 常见禁区正则片段：Qt Widgets 类型、Win32 API、窗口基类。
set(_forbid_qtwidgets "QWidget|QMainWindow|QApplication|QtWidgets")
set(_forbid_win32     "windows\\.h|windowsx\\.h|HWND|WNDPROC")

# ============================================================
# 辅助宏
# ============================================================

# _scan_scope(<forbid_regex> <glob> [<glob> ...])
# 逐文件读取 #include 行，命中 forbid_regex 即记为越界；仅扫描显式 glob。
macro(_scan_scope forbid_regex)
    set(_files "")
    foreach(_glob ${ARGN})
        file(GLOB_RECURSE _matched "${UI_SOURCE_DIR}/${_glob}")
        list(APPEND _files ${_matched})
    endforeach()

    # 路径大小写错误或目录改名将导致 glob 匹配 0 个文件；每个 scope 至少命中一个，否则按配置错误失败。
    if(NOT _files)
        message(FATAL_ERROR "Layer check scope matched no files: ${ARGN}")
    endif()

    foreach(_file IN LISTS _files)
        file(STRINGS "${_file}" _lines REGEX "^[ \t]*#include")
        foreach(_line IN LISTS _lines)
            if(_line MATCHES "${forbid_regex}")
                file(RELATIVE_PATH _rel "${UI_SOURCE_DIR}" "${_file}")
                list(APPEND _violations "${_rel}: ${_line}")
            endif()
        endforeach()
    endforeach()
endmacro()

# _scan_lines(<token_regex> <glob> [<glob> ...])
# 不限于 #include 的代码级扫描，用于检测不经过头文件的越界写法。
macro(_scan_lines token_regex)
    set(_files "")
    foreach(_glob ${ARGN})
        file(GLOB_RECURSE _matched "${UI_SOURCE_DIR}/${_glob}")
        list(APPEND _files ${_matched})
    endforeach()

    if(NOT _files)
        message(FATAL_ERROR "Line scan scope matched no files: ${ARGN}")
    endif()

    foreach(_file IN LISTS _files)
        file(STRINGS "${_file}" _lines REGEX "${token_regex}")
        foreach(_line IN LISTS _lines)
            file(RELATIVE_PATH _rel "${UI_SOURCE_DIR}" "${_file}")
            list(APPEND _violations "${_rel}: ${_line}")
        endforeach()
    endforeach()
endmacro()

# _check_contract_whitelist(<layer_name> <allowed_contract_headers...>)
# 检查某层对 Contract 的引用是否仅限于白名单头文件，防止扩大依赖面。
macro(_check_contract_whitelist layer_name)
    set(_files "")
    file(GLOB_RECURSE _files
        "${UI_SOURCE_DIR}/${_pub_root}/${layer_name}/*.h"
        "${UI_SOURCE_DIR}/src/${layer_name}/*.h"
        "${UI_SOURCE_DIR}/src/${layer_name}/*.cpp")

    if(NOT _files)
        message(FATAL_ERROR "Contract whitelist scope matched no files: ${layer_name}")
    endif()

    # 组装白名单正则：允许的头文件名用 | 分隔。
    set(_whitelist_regex "")
    foreach(_h ${ARGN})
        if(_whitelist_regex)
            string(APPEND _whitelist_regex "|")
        endif()
        string(APPEND _whitelist_regex "${_h}\\.h")
    endforeach()

    foreach(_file IN LISTS _files)
        file(STRINGS "${_file}" _lines REGEX "^[ \t]*#include.*UiShell/Contract/")
        foreach(_line IN LISTS _lines)
            if(NOT _line MATCHES "(${_whitelist_regex})")
                file(RELATIVE_PATH _rel "${UI_SOURCE_DIR}" "${_file}")
                list(APPEND _violations "${_rel}: ${_line}")
            endif()
        endforeach()
    endforeach()
endmacro()

# ============================================================
# 分层检查规则
# ============================================================

# --- contract ---
# 最底层：仅允许 QtCore 与自身，禁止反向依赖上层模块或窗口类型。
# 禁止 model/Assembly/TitleBar/platform/i18n 公共头，以及 QtWidgets 与 Win32 头。
_scan_scope("${_ref}(${_layer_model}|${_layer_assembly}|${_layer_titlebar}|${_layer_platform}|${_layer_i18n})/|${_forbid_qtwidgets}|${_forbid_win32}"
    "${_pub_root}/${_layer_contract}/*.h")

# --- model ---
# 仅允许 contract + QtCore，禁止出现界面/平台类型。
_scan_scope("${_ref}(${_layer_assembly}|${_layer_titlebar}|${_layer_platform})/|${_forbid_qtwidgets}|${_forbid_win32}"
    "src/${_layer_model}/*.h" "src/${_layer_model}/*.cpp")

# --- 公开头整体 ---
# Model 有公开头，Assembly 层需要它提供的 MenuNodeInfo；其余层的公开头禁止引用内部 Model。
_scan_scope("${_ref}${_layer_model}/"
    "${_pub_root}/Contract/*.h"
    "${_pub_root}/Platform/*.h"
    "${_pub_root}/TitleBar/*.h"
    "${_pub_root}/i18n/*.h"
    "${_pub_root}/About/*.h"
    "${_pub_root}/Export.h")

# --- TitleBar ---
# 独立类：不得依赖 model/Assembly/platform，不得直接操作 QMainWindow。
_scan_scope("${_ref}(${_layer_model}|${_layer_assembly}|${_layer_platform})/|QMainWindow|ShellRuntime|AssemblyPlanner|${_forbid_win32}"
    "${_pub_root}/${_layer_titlebar}/*.h" "src/${_layer_titlebar}/*.h" "src/${_layer_titlebar}/*.cpp")

# TitleBar 仅允许 contract 的 TitleBarSpec 与 IApiObject；其余 contract 头视为越界。
# 单独扫描是因为上面的 forbid_regex 只拦整层目录。
_check_contract_whitelist("${_layer_titlebar}" TitleBarSpec IApiObject)

# --- platform ---
# 接口头 src/Platform/WindowFrame.h 不得出现平台类型；Win32 仅限 WindowsWindowFrame.cpp。
# 非 Windows 实现同样不能引用 Win32 头，否则跨平台配置无法编译。
_scan_scope("${_forbid_win32}|QAbstractNativeEventFilter"
    "src/${_layer_platform}/WindowFrame.h")
_scan_scope("windows\\.h|windowsx\\.h|HWND"
    "src/${_layer_platform}/WindowFrameFactory.cpp" "src/${_layer_platform}/QtWindowFrame.cpp")

# --- assembly ---
# 公共头只暴露接口/前向声明；实现文件不得直接使用 Win32。
# 禁止公共头反向依赖 TitleBar/platform/i18n。
_scan_scope("${_forbid_win32}"
    "${_pub_root}/${_layer_assembly}/*.h" "src/${_layer_assembly}/*.h" "src/${_layer_assembly}/*.cpp")
_scan_scope("${_ref}(${_layer_titlebar}|${_layer_platform}|${_layer_i18n})/"
    "${_pub_root}/${_layer_assembly}/*.h")

# --- i18n ---
# 纯 QtCore：不得出现界面/平台类型，也不得依赖 model/Assembly/TitleBar/platform。
_scan_scope("${_ref}(${_layer_model}|${_layer_assembly}|${_layer_titlebar}|${_layer_platform})/|${_forbid_qtwidgets}|${_forbid_win32}"
    "${_pub_root}/${_layer_i18n}/*.h" "src/${_layer_i18n}/*.h" "src/${_layer_i18n}/*.cpp")

# --- customers（客户模块） ---
# 仅允许 contract：不得接触 model/Assembly/TitleBar/platform 内部。
_scan_scope("${_ref}(${_layer_model}|${_layer_assembly}|${_layer_titlebar}|${_layer_platform})/|QMainWindow|${_forbid_win32}"
    "customers/*/CustomerModule.cpp")

# --- dialogs（共享对话框库 UiDialogs） ---
# 链进客户库：仅允许 contract 与 Qt Widgets，不得接触 shell 内部或直接操作主窗口。
_scan_scope("${_ref}(${_layer_model}|${_layer_assembly}|${_layer_titlebar}|${_layer_platform}|${_layer_i18n})/|QMainWindow|ShellRuntime|${_forbid_win32}"
    "dialogs/*.h" "dialogs/*.cpp")

# --- shell 不认识具体对话框 ---
# 关于框等对话框在 UiDialogs / 客户库里，由客户模块按 id 注册。
_scan_scope("UiDialogs/"
    "src/*.h" "src/*.cpp" "${_pub_root}/*.h")

# --- entry app（入口） ---
# 不直接依赖 model/TitleBar/platform 内部。
# 覆盖 main/*.cpp 与 main/*/*.cpp。
_scan_scope("${_ref}(${_layer_model}|${_layer_titlebar}|${_layer_platform})/|windows\\.h|windowsx\\.h|HWND"
    "main/*.cpp" "main/*/*.cpp")

# ============================================================
# 代码级补充扫描（include 检查抓不到的口子）
# ============================================================

# 祖先窗口强转属于越界依赖：标题栏与客户模块不得向上查找主窗口。
# 用代码级扫描补住 include 检查抓不到的口子。
_scan_lines("parentWidget\\s*\\(\\s*\\)\\s*->"
    "${_pub_root}/${_layer_titlebar}/*.h" "src/${_layer_titlebar}/*.h" "src/${_layer_titlebar}/*.cpp"
    "customers/*/CustomerModule.cpp")

# 公开头不得出现标准库类型：std::string、vector、function、unique_ptr 等的布局随编译器、
# CRT 与 _ITERATOR_DEBUG_LEVEL 变化，跨 DLL 传递即内存问题（规则见 include/UiShell/Export.h）。
# 例外：Retranslatable.h / CommandTable.h（仅在客户模块内实例化），
# IMenuPresenter.h / FeatureFlag.h / WindowSpec.h / WindowFrame.h / Export.h（仅传递函数对象、智能指针、optional）。
file(GLOB_RECURSE _UI_pub_headers "${UI_SOURCE_DIR}/${_pub_root}/*.h")
list(FILTER _UI_pub_headers EXCLUDE REGEX "/(Retranslatable|CommandTable|IMenuPresenter|FeatureFlag|WindowSpec|WindowFrame|Export)\\.h$")
if(NOT _UI_pub_headers)
    message(FATAL_ERROR "Std-type check scope matched no files: ${_pub_root}")
endif()
foreach(_file IN LISTS _UI_pub_headers)
    file(STRINGS "${_file}" _lines REGEX "std::|#include[ \t]*<(string|string_view|vector|map|unordered_map|set|unordered_set|list|deque|array|memory|functional|optional|variant|any|tuple|exception|stdexcept)>")
    foreach(_line IN LISTS _lines)
        # 注释中的 std:: 不算。
        if(_line MATCHES "^[ \t]*//")
            continue()
        endif()
        file(RELATIVE_PATH _rel "${UI_SOURCE_DIR}" "${_file}")
        list(APPEND _violations "${_rel}: std type in public header: ${_line}")
    endforeach()
endforeach()

# ============================================================
# 汇总输出
# ============================================================

list(LENGTH _violations _count)
if(_count GREATER 0)
    message(STATUS "Layered dependency check failed with ${_count} violation(s):")
    foreach(_v IN LISTS _violations)
        message(STATUS "  ${_v}")
    endforeach()
    message(FATAL_ERROR "Cross-layer dependency violation found")
endif()

message(STATUS "Layered dependency check passed")
