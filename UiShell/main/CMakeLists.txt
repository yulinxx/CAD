# 宿主 EXE：target 名即产品名。
# 源文件递归自动收集，新增 .cpp 无需改动本文件。
file(GLOB_RECURSE _UI_main_src CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(${APP_TARGET} WIN32 ${_UI_main_src})

# 挂上 main/ 下所有头文件与配置文件（不编译，VS 里可查看）。
file(GLOB_RECURSE _UI_main_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
file(GLOB_RECURSE _UI_main_extra CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cmake")
set_source_files_properties(${_UI_main_headers} ${_UI_main_extra} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(${APP_TARGET} PRIVATE ${_UI_main_headers} ${_UI_main_extra})

target_include_directories(${APP_TARGET} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(${APP_TARGET} PRIVATE
    ${UI_SHELL_TARGET} ${UI_CUSTOMER_TARGET} ${UI_DIALOGS_TARGET})
target_compile_definitions(${APP_TARGET} PRIVATE
    UI_CUSTOMER_NAME="${_UI_customer_name}"
    UI_APP_VERSION="${UI_SHELL_VERSION}")

add_dependencies(${APP_TARGET} ${UI_TRANSLATIONS_TARGET})

# 部署统一走 cmake/UiDeploy.cmake：.qm 复制到 exe 旁 + windeployqt。
# 动态模式下，shell/客户 DLL 需显式交给 windeployqt 以收集其独有 Qt 依赖；
# 静态模式只需扫描宿主 EXE。
if(UI_BUILD_SHARED)
    UiDeployRuntime(${APP_TARGET}
        BINARIES ${UI_SHELL_TARGET} ${UI_CUSTOMER_TARGET})
else()
    # 静态链接：shell/客户代码已在 EXE 内，只把 QML 目录告诉 windeployqt。
    get_target_property(_UI_qml_dir ${UI_CUSTOMER_TARGET} UI_QML_DIR)
    if(NOT _UI_qml_dir OR _UI_qml_dir MATCHES "-NOTFOUND$")
        set(_UI_qml_dir "")
    endif()
    UiDeployRuntime(${APP_TARGET} QML_DIR "${_UI_qml_dir}")
endif()

# profile 外置：ui.json + assets/ 复制到 <exe>/profile/，与 ProfileLocator 查找顺序配套。
UiDeployProfile(${APP_TARGET} ${UI_CUSTOMER_TARGET})

# VS 解决方案资源管理器：按物理路径自动建虚拟文件夹。
UiVsSourceGroup(${APP_TARGET})
