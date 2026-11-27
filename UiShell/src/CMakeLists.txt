# UiShell 库：contract 接口 + 六个源码层合并编译为一个库（默认 SHARED，可切 STATIC）。
# 目标名与 Qt 版本来自上层；此处仅描述自身。

# --- 纯接口 INTERFACE ---
add_library(${UI_CONTRACT_TARGET} INTERFACE)
target_include_directories(${UI_CONTRACT_TARGET} INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include")
target_link_libraries(${UI_CONTRACT_TARGET} INTERFACE Qt${QT_VERSION_MAJOR}::Core)
if(NOT UI_BUILD_SHARED)
    target_compile_definitions(${UI_CONTRACT_TARGET} INTERFACE UI_STATIC)
endif()

# --- Model：profile 解析、校验、装配规划，内部静态库 ---
# 不导出任何符号：私有链接进 UiShell，测试直接链接本库。头文件只在 src/Model。
# src/Model 按职责分为 Profile/（解析校验加载）、Assembly/（装配规划）、
# Json/（JSONC 处理）、Spec/（值类型）四个子目录。
file(GLOB_RECURSE _UI_model_src CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Model/*.cpp")
file(GLOB_RECURSE _UI_model_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Model/*.h")
add_library(${UI_MODEL_TARGET} STATIC ${_UI_model_src})
set_source_files_properties(${_UI_model_headers} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(${UI_MODEL_TARGET} PRIVATE ${_UI_model_headers})
# 静态库会被并进 UiShell.dll / .so，需要位置无关代码。
set_target_properties(${UI_MODEL_TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)
# 以 src 为包含根，内部头写成 "Model/<子目录>/xxx.h"。
target_include_directories(${UI_MODEL_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${UI_MODEL_TARGET} PUBLIC ${UI_CONTRACT_TARGET} Qt${QT_VERSION_MAJOR}::Core)
UiVsSourceGroup(${UI_MODEL_TARGET})

# 公开头只留在 include/UiShell；内部头与对应 cpp 同放在 src/<层> 下。
# 实现文件递归收集 .cpp，天然排除 build/、translations/ 等目录。
file(GLOB_RECURSE _UI_shell_src CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
# Model 已单独编成 UiShellModel，不再直接编进 UiShell。
list(REMOVE_ITEM _UI_shell_src ${_UI_model_src})

# 平台实现二选一：Windows 同时编译 WindowsWindowFrame.cpp 与 QtWindowFrame.cpp；
# 非 Windows 仅编译 QtWindowFrame.cpp。文件本身即平台选择。
if(WIN32)
    list(REMOVE_ITEM _UI_shell_src
        "${CMAKE_CURRENT_SOURCE_DIR}/Platform/QtWindowFrame.cpp")
else()
    list(REMOVE_ITEM _UI_shell_src
        "${CMAKE_CURRENT_SOURCE_DIR}/Platform/WindowsWindowFrame.cpp")
endif()
add_library(${UI_SHELL_TARGET} ${UI_LIBRARY_TYPE} ${_UI_shell_src})

# 平台层源文件（工厂 + 当前平台实现）：平台层不导出符号。
if(WIN32)
    set(_UI_platform_impl "${CMAKE_CURRENT_SOURCE_DIR}/Platform/WindowsWindowFrame.cpp")
else()
    set(_UI_platform_impl "${CMAKE_CURRENT_SOURCE_DIR}/Platform/QtWindowFrame.cpp")
endif()
set(UI_PLATFORM_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Platform/WindowFrameFactory.cpp"
    "${_UI_platform_impl}"
    PARENT_SCOPE)

# 头文件用 HEADER_FILE_ONLY 挂上：参与显示但不参与编译；目录新增会自动出现。
file(GLOB_RECURSE _UI_shell_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/../include/UiShell/*.h")
set_source_files_properties(${_UI_shell_headers} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(${UI_SHELL_TARGET} PRIVATE ${_UI_shell_headers})

# AUTOMOC 沿各 .cpp 的 #include 找到带 Q_OBJECT 的头；src 根目录在私有包含路径里，
# 内部头以 "../<层>/xxx.h" 相对包含。
target_include_directories(${UI_SHELL_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(${UI_SHELL_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../include")
if(UI_BUILD_SHARED)
    target_compile_definitions(${UI_SHELL_TARGET} PRIVATE UI_SHELL_EXPORTS)
else()
    target_compile_definitions(${UI_SHELL_TARGET} PUBLIC UI_STATIC)
endif()

# 皮肤资源随 shell 打进 qrc，所有 DLL 共用同一份内嵌样式。
target_sources(${UI_SHELL_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/UiShell.qrc")
target_link_libraries(${UI_SHELL_TARGET} PUBLIC
    ${UI_CONTRACT_TARGET}
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Widgets)
# Model 私有链接：符号留在 UiShell 内部，不进导出表。
target_link_libraries(${UI_SHELL_TARGET} PRIVATE ${UI_MODEL_TARGET})

# 额外挂上资源目录里的 json、qss 等配置文件（不编译，VS 里可查看）。
file(GLOB_RECURSE _UI_shell_extra CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/*.qss"
    "${CMAKE_CURRENT_SOURCE_DIR}/../resources/*.svg")
if(_UI_shell_extra)
    set_source_files_properties(${_UI_shell_extra} PROPERTIES HEADER_FILE_ONLY TRUE)
    target_sources(${UI_SHELL_TARGET} PRIVATE ${_UI_shell_extra})
endif()

# VS 解决方案资源管理器：按物理路径自动建虚拟文件夹。
UiVsSourceGroup(${UI_SHELL_TARGET})
