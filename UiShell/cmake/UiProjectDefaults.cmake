# UiShell 子目录的公共默认值：编译开关、输出目录、Qt 查找、客户选择、目标名。
# 由 UiShell/CMakeLists.txt 在根工程 project() 之后 include；子目录仅描述各自目标。
#
# 与 SampleFrame 原版的差异：
#   1. 不调用 project()，版本/编译器/Qt 全部继承根工程；
#   2. 输出目录落到 <build>/bin_Qt6/shell 与 <build>/lib_Qt6/shell，不覆盖根工程；
#   3. 不挂 vcpkg toolchain（根工程已挂），只用 Qt。

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# 根工程全局 -fvisibility=hidden。外壳的契约接口是纯头文件类（无导出符号），
# 跨 dylib/EXE 的 dynamic_cast<IFoo*> 依赖 typeinfo 统一；hidden 会让每个
# 模块各生成一份本地 typeinfo，macOS/Linux 上 cast 静默失败（Windows RTTI
# 按名字比较，不受影响）。这里对本子目录放开可见性，导出面仍由 Export.h
# 的 UI_SHELL_API / UI_CUSTOMER_API 控制（CheckExports 仅 Windows 生效）。
if(NOT WIN32)
    set(CMAKE_CXX_VISIBILITY_PRESET default)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN OFF)
endif()

# 外壳独立输出目录：避免与现有 Main/UI 产物混在一起，也避免 QualityGates 误扫。
set(_UI_SHELL_BIN_DIR "${CMAKE_BINARY_DIR}/bin_Qt6/shell")
set(_UI_SHELL_LIB_DIR "${CMAKE_BINARY_DIR}/lib_Qt6/shell")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${_UI_SHELL_BIN_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${_UI_SHELL_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${_UI_SHELL_LIB_DIR}")
foreach(_ui_cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER "${_ui_cfg}" _ui_cfg_u)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_ui_cfg_u} "${_UI_SHELL_BIN_DIR}/${_ui_cfg}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${_ui_cfg_u} "${_UI_SHELL_BIN_DIR}/${_ui_cfg}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${_ui_cfg_u} "${_UI_SHELL_LIB_DIR}/${_ui_cfg}")
endforeach()

if(MSVC)
    add_compile_options(/utf-8)
endif()

# ------------------------------------------------------------
# 交付形态：默认动态库（DLL），宿主 EXE 链接 shell 与客户库。
# 单 EXE / 简化部署时显式关闭：-DUI_BUILD_SHARED=OFF。
# ------------------------------------------------------------
option(UI_BUILD_SHARED "Build UiShell/customer shared libraries" ON)
if(UI_BUILD_SHARED)
    set(UI_LIBRARY_TYPE SHARED)
else()
    set(UI_LIBRARY_TYPE STATIC)
    add_compile_definitions(UI_STATIC)
endif()

# ------------------------------------------------------------
# 客户选择：UI_CUSTOMER 值即产品正式名（大小写不敏感）。
# 合法值：MantiSoft / TyvokStudio / GoodLaser。
# ------------------------------------------------------------
set(UI_CUSTOMER "MantiSoft" CACHE STRING "UiShell customer: MantiSoft / TyvokStudio / GoodLaser")
string(TOLOWER "${UI_CUSTOMER}" _UI_customer_lower)
if(NOT _UI_customer_lower MATCHES "^(mantisoft|tyvokstudio|goodlaser)$")
    message(FATAL_ERROR "UI_CUSTOMER must be one of MantiSoft/TyvokStudio/GoodLaser, got '${UI_CUSTOMER}'")
endif()

if(_UI_customer_lower STREQUAL "mantisoft")
    set(_UI_customer_name "MantiSoft")
elseif(_UI_customer_lower STREQUAL "tyvokstudio")
    set(_UI_customer_name "TyvokStudio")
else()
    set(_UI_customer_name "GoodLaser")
endif()

set(_UI_customer_dir "${_UI_customer_name}")
set(UI_CUSTOMER_TARGET "Ui${_UI_customer_name}")
set(APP_TARGET "${_UI_customer_name}")

# ------------------------------------------------------------
# 目标名集中定义：改名只动此处；子目录通过父作用域继承。
# ------------------------------------------------------------
set(UI_CONTRACT_TARGET     "UiContract")
set(UI_SHELL_TARGET        "UiShell")
set(UI_MODEL_TARGET        "UiShellModel")
set(UI_DIALOGS_TARGET      "UiDialogs")
set(UI_TRANSLATIONS_TARGET "UiTranslations")

message(STATUS "[UiShell] customer : ${_UI_customer_name}")
message(STATUS "[UiShell] library  : ${UI_LIBRARY_TYPE}")
message(STATUS "[UiShell] output   : ${_UI_SHELL_BIN_DIR}")

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets LinguistTools)

# 供 windeployqt / offscreen 测试定位 Qt 目录。
get_target_property(_UI_qmake Qt${QT_VERSION_MAJOR}::qmake IMPORTED_LOCATION)
if(_UI_qmake)
    get_filename_component(UI_QT_BIN_DIR "${_UI_qmake}" DIRECTORY)
    get_filename_component(UI_QT_PLUGIN_DIR "${UI_QT_BIN_DIR}/../plugins" ABSOLUTE)
    get_filename_component(UI_QT_QML_DIR "${UI_QT_BIN_DIR}/../qml" ABSOLUTE)
else()
    set(UI_QT_BIN_DIR "")
    set(UI_QT_PLUGIN_DIR "")
    set(UI_QT_QML_DIR "")
endif()

# dumpbin 与 link.exe 同目录：部署自检时用它读取二进制的导入表。
if(WIN32 AND CMAKE_LINKER)
    get_filename_component(_UI_linker_dir "${CMAKE_LINKER}" DIRECTORY)
    if(EXISTS "${_UI_linker_dir}/dumpbin.exe")
        set(UI_DUMPBIN "${_UI_linker_dir}/dumpbin.exe")
    endif()
endif()

# ------------------------------------------------------------
# 客户库源码收集：递归收集客户目录下的 .cpp 与 .qrc。
# ------------------------------------------------------------
function(msUiCustomerSources out_var)
    file(GLOB_RECURSE _UI_customer_src CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.qrc")
    if(NOT _UI_customer_src)
        message(FATAL_ERROR
            "msUiCustomerSources: no source found under ${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
    set(${out_var} ${_UI_customer_src} PARENT_SCOPE)
endfunction()

# ------------------------------------------------------------
# 客户库装配：建库 + 收集源码 + 终结配置。
#   msUiAddCustomerLibrary(<target> [QML_DIR <dir>] [QML_LIBS <lib>...])
# ------------------------------------------------------------
function(msUiAddCustomerLibrary target)
    cmake_parse_arguments(ARG "" "QML_DIR" "QML_LIBS" ${ARGN})

    msUiCustomerSources(_UI_customer_src)
    add_library(${target} ${UI_LIBRARY_TYPE} ${_UI_customer_src})
    set_target_properties(${target} PROPERTIES UNITY_BUILD OFF)

    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

    set(_finalize_args)
    if(ARG_QML_DIR)
        list(APPEND _finalize_args QML_DIR "${ARG_QML_DIR}")
    endif()
    if(ARG_QML_LIBS)
        list(APPEND _finalize_args QML_LIBS ${ARG_QML_LIBS})
    endif()
    msUiFinalizeCustomer(${target} ${_finalize_args})
endfunction()

# ------------------------------------------------------------
# 客户目标终结函数：统一完成 profile、导出宏、依赖链接与 IDE 分组。
# ------------------------------------------------------------
function(msUiFinalizeCustomer target)
    cmake_parse_arguments(ARG "" "QML_DIR" "QML_LIBS" ${ARGN})

    UiCustomerProfile(${target})

    if(UI_BUILD_SHARED)
        target_compile_definitions(${target} PRIVATE UI_CUSTOMER_EXPORTS)
    endif()

    target_link_libraries(${target} PUBLIC
        ${UI_CONTRACT_TARGET}
        Qt${QT_VERSION_MAJOR}::Widgets)
    if(ARG_QML_LIBS)
        target_link_libraries(${target} PUBLIC ${ARG_QML_LIBS})
    endif()
    target_link_libraries(${target} PRIVATE ${UI_DIALOGS_TARGET})

    if(ARG_QML_DIR)
        set_target_properties(${target} PROPERTIES UI_QML_DIR "${ARG_QML_DIR}")
    endif()

    file(GLOB_RECURSE _extra CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.qml")
    if(_extra)
        set_source_files_properties(${_extra} PROPERTIES HEADER_FILE_ONLY TRUE)
        target_sources(${target} PRIVATE ${_extra})
    endif()

    UiVsSourceGroup(${target})
endfunction()
