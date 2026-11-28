# UiDialogs：算法与业务对话框的共享默认实现（静态库）。
# 链进各客户库，不单独部署、不导出符号；仅依赖 UiContract 与 Qt Widgets。
# 同一进程里仅能有一个运行时模块链接它；客户在 CustomerModule::owner 里持有工厂实例。
# 需 UiShell/EXE 使用或改为多客户插件时，应改为跟随 UI_BUILD_SHARED 的动态库。
# 客户在 createCustomerModule() 里注册工厂实例；需要不同界面的客户用同一 dialogId 注册自己的实现即可覆盖。
#   include/UiDialogs/  客户可见的头：对话框 id、工厂、命令处理、算法与参数
#   src/                实现与内部头
file(GLOB_RECURSE _UI_dialogs_src CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE _UI_dialogs_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")

add_library(${UI_DIALOGS_TARGET} STATIC ${_UI_dialogs_src})
set_source_files_properties(${_UI_dialogs_headers} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(${UI_DIALOGS_TARGET} PRIVATE ${_UI_dialogs_headers})

# 静态库会被并进客户 DLL / .so，需要位置无关代码。
set_target_properties(${UI_DIALOGS_TARGET} PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(${UI_DIALOGS_TARGET} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${UI_DIALOGS_TARGET} PUBLIC
    ${UI_CONTRACT_TARGET}
    Qt${QT_VERSION_MAJOR}::Widgets)

UiVsSourceGroup(${UI_DIALOGS_TARGET})
