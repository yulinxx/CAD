# 运行期部署：所有部署动作集中在此，子目录不写散落的 POST_BUILD。
#   UiDeployRuntime(<target> [BINARIES <lib-target>...] [QML_DIR <dir>])
# 动态链接时把 shell/客户库一并放进 BINARIES，windeployqt 才会收集它们独有的 Qt 依赖；
# 静态模式下只扫描宿主 EXE，QML 目录由调用方显式传入。
# .qm 放在 <exe>/translations 便于现场替换；windeployqt 结束后执行构建期自检（VerifyDeploy.cmake）。

option(UI_DEPLOY_QT "Deploy Qt runtime with windeployqt after build" ON)

# 文件级记录脚本目录：函数体内 CMAKE_CURRENT_LIST_DIR 指向调用方。
set(_UI_deploy_script_dir "${CMAKE_CURRENT_LIST_DIR}")

function(UiDeployRuntime target)
    cmake_parse_arguments(ARG "" "QML_DIR" "BINARIES" ${ARGN})

    # 一起扫描的二进制：宿主 EXE + 可选的 shell/客户 DLL。
    set(_binaries "$<TARGET_FILE:${target}>")
    set(_qmldir "${ARG_QML_DIR}")
    foreach(_bin IN LISTS ARG_BINARIES)
        list(APPEND _binaries "$<TARGET_FILE:${_bin}>")
        # 仅带 QML 的库设置该属性，否则保持纯导入表部署。
        get_target_property(_qml_dir ${_bin} UI_QML_DIR)
        if(_qml_dir AND NOT _qml_dir MATCHES "-NOTFOUND$")
            set(_qmldir "${_qml_dir}")
        endif()
    endforeach()

    # .qm 拷到 exe 旁的 translations，便于现场替换语言包。
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:${target}>/translations"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${UI_QM_FILES} "$<TARGET_FILE_DIR:${target}>/translations"
        COMMENT "Copy .qm translations next to ${target}")

    if(WIN32 AND UI_DEPLOY_QT AND UI_QT_BIN_DIR)
        # 直接从 Qt bin 目录推导，不用 find_program（缓存会导致跨 Qt 版本指向旧工具）。
        set(_windeployqt "${UI_QT_BIN_DIR}/windeployqt${CMAKE_EXECUTABLE_SUFFIX}")
        if(EXISTS "${_windeployqt}")
            # --qmldir 在 Qt5/Qt6 的 windeployqt 上都可用，调用 qmlimportscanner
            # 按 .qml 的 import 收集 qml 模块目录与插件依赖。
            # 注意：windeployqt 只对「自身直接依赖 QtQml/Quick」的二进制做 QML 扫描，
            # 客户 DLL 才是 QML 持有者，宿主 EXE 不是，因此逐个二进制各执行一次。
            set(_qml_args "")
            if(_qmldir)
                set(_qml_args --qmldir "${_qmldir}")
            endif()

            set(_deploy_commands "")
            foreach(_bin IN LISTS _binaries)
                list(APPEND _deploy_commands
                    COMMAND "${_windeployqt}"
                        --no-translations --no-system-d3d-compiler --no-opengl-sw
                        ${_qml_args}
                        "${_bin}")
            endforeach()

            add_custom_command(TARGET ${target} POST_BUILD
                ${_deploy_commands}
                COMMENT "windeployqt: deploy Qt runtime (exe + linked DLLs)")

            # 构建期自检：每个部署过的二进制导入的 Qt 模块必须落在输出目录。
            # 仅检查本次部署的 exe + 客户 DLL；测试可执行文件不参与。
            if(UI_DUMPBIN)
                set(_need_qml FALSE)
                if(_qmldir)
                    set(_need_qml TRUE)
                endif()
                # 生成期不能求值 genex，用逗号拼成一个参数（Windows 路径不含逗号）。
                set(_verify_bins "")
                foreach(_bin IN LISTS _binaries)
                    if(_verify_bins)
                        set(_verify_bins "${_verify_bins},${_bin}")
                    else()
                        set(_verify_bins "${_bin}")
                    endif()
                endforeach()
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND}
                        -DUI_DEPLOY_DIR="$<TARGET_FILE_DIR:${target}>"
                        -DUI_DUMPBIN="${UI_DUMPBIN}"
                        -DUI_BINARIES=${_verify_bins}
                        -DUI_NEED_QML=${_need_qml}
                        -P "${_UI_deploy_script_dir}/VerifyDeploy.cmake"
                    COMMENT "Verify Qt deployment completeness")
            endif()
        else()
            message(WARNING "windeployqt not found at ${_windeployqt}; Qt runtime not deployed")
        endif()
    endif()
endfunction()

# profile 外置部署：把客户的 ui.json 与 assets/ 复制到 <exe>/profile/。
#   UiDeployProfile(<exe-target> <customer-target>)
# 启动时 ProfileLocator 按 --profile → <exe>/profile/ui.json → 客户库内嵌资源的顺序查找；
# 此处放置的副本让现场可以直接改配置。与 .qm 复制一样，不受 UI_DEPLOY_QT 影响。
function(UiDeployProfile target customer_target)
    get_target_property(_dir ${customer_target} UI_PROFILE_DIR)
    if(NOT _dir OR _dir MATCHES "-NOTFOUND$")
        message(FATAL_ERROR
            "UiDeployProfile: ${customer_target} has no UI_PROFILE_DIR (call UiCustomerProfile first)")
    endif()

    set(_dst "$<TARGET_FILE_DIR:${target}>/profile")
    set(_commands
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dst}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dir}/ui.json" "${_dst}/ui.json")
    if(IS_DIRECTORY "${_dir}/assets")
        list(APPEND _commands
            COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different "${_dir}/assets" "${_dst}/assets")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        ${_commands}
        COMMENT "Copy UI profile (ui.json + assets) next to ${target}")
endfunction()
