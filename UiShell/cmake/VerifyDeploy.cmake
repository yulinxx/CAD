# 部署自检（构建期执行）：确保每个自研二进制导入的 Qt 模块已落在输出目录。
# 被 cmake/UiDeploy.cmake 在 windeployqt 之后调用。
# 输入：UI_DEPLOY_DIR（部署目录）、UI_DUMPBIN、UI_BINARIES（逗号分隔）、UI_NEED_QML。
# 仅检查 Qt*.dll；系统 DLL 与自研 DLL 不在此脚本职责内。

if(NOT UI_DEPLOY_DIR OR NOT UI_DUMPBIN OR NOT UI_BINARIES)
    message(FATAL_ERROR "VerifyDeploy: missing UI_DEPLOY_DIR / UI_DUMPBIN / UI_BINARIES")
endif()
if(NOT EXISTS "${UI_DEPLOY_DIR}")
    message(FATAL_ERROR "VerifyDeploy: deploy dir not found: ${UI_DEPLOY_DIR}")
endif()

string(REPLACE "," ";" _binaries "${UI_BINARIES}")

set(_missing "")
foreach(_bin IN LISTS _binaries)
    get_filename_component(_name "${_bin}" NAME)
    # Qt 自身的 DLL 不需要再查，它们的依赖由 windeployqt 保证。
    if(_name MATCHES "^[Qq]t[56]")
        continue()
    endif()

    execute_process(
        COMMAND "${UI_DUMPBIN}" /dependents "${_bin}"
        OUTPUT_VARIABLE _dump
        ERROR_QUIET
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "VerifyDeploy: dumpbin failed on ${_bin}")
    endif()

    # 导入表里的 Qt 模块名（Debug 带 d 后缀）：校验确切文件名，确保 Debug/Release 一致。
    string(REGEX MATCHALL "[Qq]t[56][A-Za-z0-9_]*\\.dll" _qt_deps "${_dump}")
    foreach(_dep IN LISTS _qt_deps)
        if(NOT EXISTS "${UI_DEPLOY_DIR}/${_dep}")
            list(APPEND _missing "${_dep}  (needed by ${_name})")
        endif()
    endforeach()
endforeach()

if(UI_NEED_QML)
    # QML 模块目录落点随 Qt 版本/工具不同，接受 qml/QtQuick、qml/QtQuick.2、QtQuick、QtQuick.2 四种。
    set(_qml_ok FALSE)
    foreach(_rel IN ITEMS "qml/QtQuick" "qml/QtQuick.2" "QtQuick" "QtQuick.2")
        if(IS_DIRECTORY "${UI_DEPLOY_DIR}/${_rel}")
            set(_qml_ok TRUE)
        endif()
    endforeach()
    if(NOT _qml_ok)
        list(APPEND _missing "QtQuick qml 模块目录  (windeployqt --qmldir 未生效)")
    endif()
endif()

if(_missing)
    list(REMOVE_DUPLICATES _missing)
    string(REPLACE ";" "\n  " _text "${_missing}")
    message(FATAL_ERROR
        "Qt 部署不完整，运行时会加载失败：\n  ${_text}\n"
        "部署目录：${UI_DEPLOY_DIR}")
endif()

message(STATUS "Qt deployment verified: ${UI_DEPLOY_DIR}")
