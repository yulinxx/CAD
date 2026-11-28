# ============================================================
# UiShell：以 SampleFrame 为蓝本的配置驱动 UI 外壳。
#
# 本目录是 April 根工程的一个子目录（不调用 project()），
# 复用根工程已解析的 Qt6 / vcpkg / 编译器设置，输出到独立目录：
#   <build>/bin_Qt6/shell/    EXE 与动态库
#   <build>/lib_Qt6/shell/    静态库
#
# 迁移策略：新外壳先与现有 Main 并行存在，Main 保持可运行；
# 随后按里程碑把 Main 的工作台/视口/面板逐步搬到 customers/April。
# ============================================================

set(UI_SHELL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(UI_SHELL_BUILD_DIR  "${CMAKE_CURRENT_BINARY_DIR}")
set(UI_SHELL_VERSION    "${PROJECT_VERSION}")
if(NOT UI_SHELL_VERSION)
    set(UI_SHELL_VERSION "1.0.0")
endif()

# 根工程开启了 CMAKE_UNITY_BUILD；外壳代码按“一文件一符号”组织，
# 合批会制造静态符号冲突，这里对本子目录关闭。
set(CMAKE_UNITY_BUILD OFF)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiProjectDefaults.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiBuildInfo.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiTranslations.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiDeploy.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiCustomerProfile.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/UiVsSourceGroup.cmake")

add_subdirectory(src)
# 共享对话框库（静态）：算法与业务对话框的默认实现，链进客户库。
add_subdirectory(dialogs)
add_subdirectory("customers/${_UI_customer_dir}")
add_subdirectory(main)

# ---------------------------------------------------------------------------
# M6：产品目标 April 现在直接用 UiShell/main 入口，但目标在 Main/ 子目录创建，
# UiDeployRuntime() 的 POST_BUILD 助手要求目标与调用在同一目录，因此这里用
# 一个 ALL 自定义目标承担 April 的运行期部署（与 AprilShell 的部署互不干扰）。
# ---------------------------------------------------------------------------
if(UI_CUSTOMER STREQUAL "April" AND TARGET April AND TARGET ${UI_CUSTOMER_TARGET})
    get_target_property(_april_profile_dir ${UI_CUSTOMER_TARGET} UI_PROFILE_DIR)
    set(_april_deploy_cmds
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:April>/translations")
    if(UI_QM_FILES)
        list(APPEND _april_deploy_cmds
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${UI_QM_FILES} "$<TARGET_FILE_DIR:April>/translations")
    endif()
    if(_april_profile_dir AND NOT _april_profile_dir MATCHES "-NOTFOUND$")
        list(APPEND _april_deploy_cmds
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:April>/profile"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_april_profile_dir}/ui.json" "$<TARGET_FILE_DIR:April>/profile/ui.json")
        foreach(_subdir assets styles translations)
            if(IS_DIRECTORY "${_april_profile_dir}/${_subdir}")
                list(APPEND _april_deploy_cmds
                    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
                        "${_april_profile_dir}/${_subdir}" "$<TARGET_FILE_DIR:April>/profile/${_subdir}")
            endif()
        endforeach()
    endif()

    set(_april_qml_args "")
    if(UI_BUILD_SHARED)
        get_target_property(_april_qml_dir ${UI_CUSTOMER_TARGET} UI_QML_DIR)
        if(_april_qml_dir AND NOT _april_qml_dir MATCHES "-NOTFOUND$")
            set(_april_qml_args --qmldir "${_april_qml_dir}")
        endif()
    endif()

    if(WIN32 AND UI_DEPLOY_QT AND UI_QT_BIN_DIR)
        set(_windeployqt "${UI_QT_BIN_DIR}/windeployqt${CMAKE_EXECUTABLE_SUFFIX}")
        if(EXISTS "${_windeployqt}")
            set(_april_deploy_bins "$<TARGET_FILE:April>")
            if(UI_BUILD_SHARED)
                list(APPEND _april_deploy_bins
                    "$<TARGET_FILE:${UI_SHELL_TARGET}>"
                    "$<TARGET_FILE:${UI_CUSTOMER_TARGET}>")
            endif()
            foreach(_bin IN LISTS _april_deploy_bins)
                list(APPEND _april_deploy_cmds
                    COMMAND "${_windeployqt}"
                        --no-translations --no-system-d3d-compiler --no-opengl-sw
                        ${_april_qml_args} "${_bin}")
            endforeach()
        endif()
    endif()

    add_custom_target(AprilRuntimeDeploy ALL
        DEPENDS April ${UI_TRANSLATIONS_TARGET}
        ${_april_deploy_cmds}
        COMMENT "Deploy April runtime assets (UiShell profile + translations)")
endif()

# 测试：根工程用 BUILD_TESTS；独立构建时也接受 SampleFrame 的 Enable_TEST。
if(BUILD_TESTS OR Enable_TEST)
    enable_testing()
    add_subdirectory(tests)
endif()
