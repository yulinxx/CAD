# 构建指纹：防止 EXE、shell DLL、客户 DLL 混装。
# 仅包含影响二进制兼容性的事实（ABI 版本、Qt、编译器、架构、客户、链接形态、构建 id），
# 不含源码路径与时间戳，保证同一提交 + 同一工具链可复现。
# 覆盖入口：-DUI_BUILD_ID=<发行标识>。
#
# 与 SampleFrame 原版的差异：路径以 UI_SHELL_SOURCE_DIR 为准，
# 版本取根工程 PROJECT_VERSION（UI_SHELL_VERSION 兜底）。

# ABI 版本从公开头读取，避免 CMake 与 C++ 常量各维护一份。
file(STRINGS "${UI_SHELL_SOURCE_DIR}/include/UiShell/Contract/IApiObject.h"
    _UI_api_version_line REGEX "kUiApiVersion[ \t]*=")
if(NOT _UI_api_version_line)
    message(FATAL_ERROR "cannot read kUiApiVersion from IApiObject.h")
endif()
string(REGEX MATCH "[0-9]+" UI_ABI_VERSION "${_UI_api_version_line}")

if(NOT UI_BUILD_ID)
    set(UI_BUILD_ID "")

    # 1) 尝试获取 git HEAD commit
    execute_process(
        COMMAND git -C "${UI_SHELL_SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE _UI_git_commit
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE _UI_git_result)

    # 2) 检测工作区是否干净：脏树时 commit 无法唯一标识二进制
    execute_process(
        COMMAND git -C "${UI_SHELL_SOURCE_DIR}" status --porcelain
        OUTPUT_VARIABLE _UI_git_dirty
        OUTPUT_QUIET
        ERROR_QUIET)

    if(_UI_git_result EQUAL 0 AND _UI_git_commit AND NOT _UI_git_dirty)
        set(UI_BUILD_ID "${_UI_git_commit}")
    else()
        # 3) 回退：源码内容哈希。同一份源码 → 同一指纹（可复现）；
        #    源码改动 → 指纹变化，可检测到未提交的变更。
        file(GLOB_RECURSE _UI_hash_inputs RELATIVE "${UI_SHELL_SOURCE_DIR}"
            "${UI_SHELL_SOURCE_DIR}/cmake/*.cmake"
            "${UI_SHELL_SOURCE_DIR}/CMakeLists.txt"
            "${UI_SHELL_SOURCE_DIR}/include/**"
            "${UI_SHELL_SOURCE_DIR}/src/**"
            "${UI_SHELL_SOURCE_DIR}/main/**"
            "${UI_SHELL_SOURCE_DIR}/customers/**")
        list(SORT _UI_hash_inputs)
        set(_UI_concat "")
        foreach(_f IN LISTS _UI_hash_inputs)
            file(SHA256 "${UI_SHELL_SOURCE_DIR}/${_f}" _h)
            string(APPEND _UI_concat "${_f}:${_h};")
        endforeach()
        string(SHA256 _UI_content_hash "${_UI_concat}")
        string(SUBSTRING "${_UI_content_hash}" 0 12 _UI_content_hash)
        set(UI_BUILD_ID "src-${_UI_content_hash}")
    endif()

    set(UI_BUILD_ID "${UI_BUILD_ID}" CACHE STRING
        "Build id embedded in the fingerprint (override per release)")
endif()

# find_package(Qt6 ...) 在 UiProjectDefaults.cmake 中完成；直接取解析出的 Core 版本。
set(UI_QT_COMPILE_VERSION "${Qt${QT_VERSION_MAJOR}Core_VERSION}")
if(NOT UI_QT_COMPILE_VERSION)
    set(UI_QT_COMPILE_VERSION "${QT_VERSION}")
endif()

# CRT 配置：/MT 与 /MD 分配的内存不能跨模块释放，指纹必须区分。
set(_UI_crt "${CMAKE_MSVC_RUNTIME_LIBRARY}")
if(NOT _UI_crt)
    set(_UI_crt "default")
else()
    string(REPLACE ";" "_" _UI_crt "${_UI_crt}")
endif()

set(UI_BUILD_FINGERPRINT_BASE
    "msui|api=${UI_ABI_VERSION}|version=${UI_SHELL_VERSION}|qt=${UI_QT_COMPILE_VERSION}|compiler=${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}|arch=${CMAKE_SIZEOF_VOID_P}|customer=${_UI_customer_name}|shared=${UI_BUILD_SHARED}|crt=${_UI_crt}|build=${UI_BUILD_ID}")

set(UI_BUILD_INFO_DIR "${UI_SHELL_BUILD_DIR}/generated")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/UiBuildInfo.h.in"
    "${UI_BUILD_INFO_DIR}/UiBuildInfo.h"
    @ONLY)

# shell、客户、EXE、测试都编译同一份生成头，指纹值才可比较。
include_directories("${UI_BUILD_INFO_DIR}")

# 构建清单：随二进制放到 shell 输出目录，现场可核对加载的是哪次构建。
file(GENERATE
    OUTPUT "${_UI_SHELL_BIN_DIR}/$<CONFIG>/customer-build-manifest.json"
    CONTENT "{
  \"schema\": 1,
  \"project\": \"UiShell\",
  \"version\": \"${UI_SHELL_VERSION}\",
  \"customer\": \"${_UI_customer_name}\",
  \"shared\": \"${UI_BUILD_SHARED}\",
  \"crtConfig\": \"${_UI_crt}\",
  \"qtCompileVersion\": \"${UI_QT_COMPILE_VERSION}\",
  \"compiler\": \"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\",
  \"architectureBits\": \"${CMAKE_SIZEOF_VOID_P}\",
  \"abiVersion\": \"${UI_ABI_VERSION}\",
  \"buildId\": \"${UI_BUILD_ID}\",
  \"config\": \"$<CONFIG>\",
  \"fingerprint\": \"${UI_BUILD_FINGERPRINT_BASE}|config=$<IF:$<CONFIG:Debug>,Debug,Release>\"
}
")
