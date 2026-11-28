# --------------------------------------------------------------------
# AprDeployQt.cmake —— Qt 运行时部署脚本（由 apr_deploy_qt_dlls 以 cmake -P 调用）
#
# 背景：所有可执行目标输出到同一 bin 目录，并行构建时多个目标的 POST_BUILD
# 同时运行 windeployqt / 复制 offscreen 插件，争用同一批 DLL，
# 报 "The process cannot access the file because it is being used by another process"
# （MSB3073，退出码 32）。此处用跨进程文件锁将部署串行化。
#
# 参数：
#   LOCK_FILE         锁文件路径（同一输出目录的所有目标须使用同一个）
#   WINDEPLOYQT       windeployqt.exe 路径（为空则跳过）
#   TARGET_FILE       目标二进制路径
#   OUTPUT_DIR        部署目录
#   OFFSCREEN_PLUGIN  qoffscreen(d).dll 路径（为空或不存在则跳过）
# --------------------------------------------------------------------

if(NOT LOCK_FILE)
    message(FATAL_ERROR "[AprDeployQt] LOCK_FILE is not set")
endif()

file(LOCK "${LOCK_FILE}" GUARD FILE TIMEOUT 600 RESULT_VARIABLE _lock_rc)
if(NOT _lock_rc STREQUAL "0")
    message(FATAL_ERROR "[AprDeployQt] Failed to acquire deploy lock: ${_lock_rc}")
endif()

if(WINDEPLOYQT AND EXISTS "${WINDEPLOYQT}")
    execute_process(
        COMMAND "${WINDEPLOYQT}"
            --no-translations
            --no-system-d3d-compiler
            --no-opengl-sw
            --no-quick
            --no-quick-import
            --dir "${OUTPUT_DIR}"
            "${TARGET_FILE}"
        RESULT_VARIABLE _rc
    )
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "[AprDeployQt] windeployqt failed (${_rc}): ${TARGET_FILE}")
    endif()
endif()

# 无头测试需要 offscreen 平台插件：windeployqt 默认只部署 qwindows
if(OFFSCREEN_PLUGIN AND EXISTS "${OFFSCREEN_PLUGIN}")
    file(MAKE_DIRECTORY "${OUTPUT_DIR}/platforms")
    get_filename_component(_plugin_name "${OFFSCREEN_PLUGIN}" NAME)
    file(COPY_FILE "${OFFSCREEN_PLUGIN}" "${OUTPUT_DIR}/platforms/${_plugin_name}" ONLY_IF_DIFFERENT)
endif()
