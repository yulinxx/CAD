# ============================================================================
# 2D 场景写面门禁 — SceneEditService 是唯一写入口
#
# 背景：
#   `Eg::SceneManager` 的实体增删接口已降为 private，只有两个访问者：
#     - `SceneEditService`  ← UI/Main 生产代码
#     - `Eg::SceneWriteGateway` ← Engine2D 内部与模块内测试
#   private 已经在编译期挡住了生产代码；本检查是第二道，防止有人把
#   `SceneWriteGateway.h` 重新 include 进生产代码（绕过唯一写入口）。
#
# 口径：
#   - 只扫「生产代码」目录（不含 Test/Stub）；
#   - 命中 `SceneWriteGateway` 即 configure 失败；
#   - 需要直写场景的内部实现请放在 Engine/2D/Src（已在门禁范围外）。
#
# 为什么不用正则扫 `->addEntity(`：
#   同名方法在 `AprGroup` / `AprLayer` / `DocumentTransaction` 上也存在，
#   文本匹配会大面积误报；include 白名单是唯一无歧义的判据。
# ============================================================================

function(apr_check_scene_write_surface)
    set(_root "${APR_ROOT}")
    set(_scan_dirs
        "${_root}/UI/2D/Src"
        "${_root}/UI/Common/Src"
        "${_root}/Main/Src"
        "${_root}/PyBindCore/PyBindCore/Src"
    )

    set(_files "")
    foreach(_dir IN LISTS _scan_dirs)
        if(EXISTS "${_dir}")
            file(GLOB_RECURSE _dir_files
                "${_dir}/*.cpp" "${_dir}/*.h" "${_dir}/*.mm")
            list(APPEND _files ${_dir_files})
        endif()
    endforeach()

    if(NOT _files)
        return()
    endif()

    # 测试与测试桩属于「模块内」，允许直写（它们驱动的是存储层本身）
    list(FILTER _files EXCLUDE REGEX "[/\\\\](Test|Stub|test)[/\\\\]")

    set(_violations "")
    foreach(_file IN LISTS _files)
        file(READ "${_file}" _content)
        if(_content MATCHES "SceneWriteGateway")
            file(RELATIVE_PATH _rel "${_root}" "${_file}")
            list(APPEND _violations "${_rel}")
        endif()
    endforeach()

    if(_violations)
        string(REPLACE ";" "\n  - " _violation_text "${_violations}")
        message(FATAL_ERROR
            "Production code must not use SceneWriteGateway (SceneEditService is the only write entry point).\n"
            "Violating files:\n  - ${_violation_text}\n"
            "Fix: use SceneEditService instead (CommitMode::Direct when undo is not needed, "
            "insertEntityPreserveId to preserve ids).")
    endif()

    list(LENGTH _files _file_count)
    message(STATUS "[scene write surface] production code does not use SceneWriteGateway (${_file_count} files scanned)")
endfunction()
