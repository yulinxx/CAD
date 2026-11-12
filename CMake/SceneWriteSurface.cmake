# ============================================================================
# 场景写面门禁 — SceneEditService / SceneEditService3D 是唯一生产写入口
#
# 背景：
#   `Eg::SceneManager`（2D）与 `Eg::SceneManager3D`（3D）的实体增删接口都已
#   降为 private：
#     - 2D：只 friend `SceneEditService`（UI/Main 生产）与
#       `Eg::SceneWriteGateway`（Engine2D 内部与模块内测试）；
#     - 3D：只 friend `SceneEditService3D` 经 `Eg::SceneWriteGateway3D`
#       （Engine3D 内部与模块内测试）。
#   private 已在编译期挡住了生产代码；本检查是第二道防线。
#
# 口径：
#   - 只扫「生产代码」目录（不含 Test/Stub）；
#   - 引用模块内写网关即失败；
#   - 直调场景结构写接口即失败；
#   - 3D 的 `SceneEditService3D::*Direct` 直通入口只允许白名单文件调用。
#   - **扫描到 0 个文件不能算通过**：模块 target 存在却扫不到文件说明扫描
#     目录写错了（configure 失败）；模块没建则打印显式 SKIP。
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
    set(_raw_count 0)
    foreach(_dir IN LISTS _scan_dirs)
        if(EXISTS "${_dir}")
            file(GLOB_RECURSE _dir_files
                "${_dir}/*.cpp" "${_dir}/*.h" "${_dir}/*.mm")
            list(APPEND _files ${_dir_files})
        endif()
    endforeach()
    list(LENGTH _files _raw_count)

    # 测试与测试桩属于「模块内」，允许直写（它们驱动的是存储层本身）
    list(FILTER _files EXCLUDE REGEX "[/\\\\](Test|Stub|test)[/\\\\]")
    list(LENGTH _files _file_count)
    math(EXPR _skipped_count "${_raw_count} - ${_file_count}")

    if(NOT _files)
        if(TARGET UI2D)
            message(FATAL_ERROR
                "2D scene write gate scanned 0 production files but target UI2D exists.\n"
                "Scan dirs: ${_scan_dirs}\n"
                "This usually means the gate's scan path is stale — a silently skipped gate is worse "
                "than a failing one. Fix the scan path or explicitly disable the gate.")
        else()
            message(STATUS "[scene write surface] skipped: UI2D target is not built and 0 files were scanned")
        endif()
        # 注意：不 return，3D 门禁必须无条件执行
        apr_check_scene_write_surface_3d()
        return()
    endif()

    set(_violations "")
    foreach(_file IN LISTS _files)
        file(READ "${_file}" _content)
        # SceneWriteGateway3D 由 3D 门禁负责，这里先排除，避免 2D 报错文案串台
        if(_content MATCHES "SceneWriteGateway3D")
            continue()
        endif()
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

    message(STATUS
        "[scene write surface] production code does not use SceneWriteGateway "
        "(scanned=${_file_count} skipped=${_skipped_count} allowlist=0)")

    # 3D 写面门禁与 2D 一并执行（同一入口，避免两处各调一次）
    apr_check_scene_write_surface_3d()
endfunction()

# ============================================================================
# 3D 场景写面门禁 — SceneEditService3D 是唯一生产写入口
#
# 背景：
#   `Eg::SceneManager3D` 的结构写接口（addEntity/addEntities/removeEntity/
#   deleteEntities/extractEntities）已降为 private，只 friend 引擎内网关
#   `Eg::SceneWriteGateway3D`（SceneManager3D.h 不出现任何 UI 类名）。
#   UI3D 的 `SceneEditService3D` 与撤销命令都经网关写。
#
# 检查项：
#   1) 生产代码引用 `SceneWriteGateway3D` 必须在白名单内（见下）；
#   2) `UI/3D/Src` 不得直调上述 5 个结构写接口；
#   3) Main 里的 3D 场景句柄不得直调上述 5 个结构写接口；
#   4) `SceneEditService3D::*Direct` 旁路只允许白名单文件调用。
#
# 为什么 3D 网关用白名单、2D 网关用「出现即失败」：
#   2D 的生产路径全部收敛到 SceneEditService，没有任何 UI 生产文件需要直接
#   碰 SceneWriteGateway，所以「出现即失败」是最紧的口径。
#   3D 的 `SceneEditService3D` 位于 UI3D，但它的结构写必须经过引擎内网关
#   （SceneManager3D.h 不允许知道 UI 类名），因此「编辑服务本身」必须能引用
#   网关 —— 这是设计上唯一合法的 UI 侧引用，不是漏改。除它之外，UI/Main 的
#   生产代码一律不得出现网关；撤销命令在 Engine3D 内部，同样登记在白名单里
#   以便未来把扫描范围扩到 Engine/3D/Src 时口径不变。
#
# 为什么 UI/3D/Src 可以直接扫 `->addEntity(`：
#   3D 场景没有 AprGroup/AprLayer 那种同名方法。
# ============================================================================

function(apr_check_scene_write_surface_3d)
    set(_root "${APR_ROOT}")
    set(_scan_dirs
        "${_root}/UI/3D/Src"
        "${_root}/Main/Src"
    )

    # 允许引用 SceneWriteGateway3D 的生产文件（测试目录不扫，天然允许）
    set(_gateway_allowlist
        "UI/3D/Src/Edit/SceneEditService3D.cpp"       # UI 侧唯一结构写入口
        "Engine/3D/Src/Edit/SceneUndoCommands3D.cpp"  # 撤销栈回放
    )

    # 允许调用 SceneEditService3D::*Direct 的生产文件。
    # 这些路径没有可用的 SceneEditService3D 实例（文档级装载/替换、场景树节点
    # 增删、布尔/分割的操作数搬运、导入回退），或本身就是撤销命令的语义。
    # 新文件要加进来必须先说明「为什么不能走可撤销入口」。
    set(_direct_allowlist
        "UI/3D/Src/Storage/SceneDocumentIO3D.cpp"
        "UI/3D/Src/Service/SceneDocument3D.cpp"
        "UI/3D/Src/Operation/ModelOperations3D.cpp"
        "UI/3D/Src/Operation/ModelSplit3D.cpp"
        "UI/3D/Src/Operation/FileOperations3D.cpp"
        "Main/Src/UI/Workbench/Workbench3D.cpp"
        "Main/Src/Import/ImportService.cpp"
    )

    set(_files "")
    set(_raw_count 0)
    foreach(_dir IN LISTS _scan_dirs)
        if(EXISTS "${_dir}")
            file(GLOB_RECURSE _dir_files
                "${_dir}/*.cpp" "${_dir}/*.h" "${_dir}/*.mm")
            list(APPEND _files ${_dir_files})
        endif()
    endforeach()
    list(LENGTH _files _raw_count)

    list(FILTER _files EXCLUDE REGEX "[/\\\\](Test|Stub|test)[/\\\\]")
    list(LENGTH _files _file_count)
    math(EXPR _skipped_count "${_raw_count} - ${_file_count}")
    list(LENGTH _direct_allowlist _direct_allowlist_count)
    list(LENGTH _gateway_allowlist _gateway_allowlist_count)
    math(EXPR _allowlist_count "${_direct_allowlist_count} + ${_gateway_allowlist_count}")

    if(NOT _files)
        if(TARGET UI3D)
            message(FATAL_ERROR
                "3D scene write gate scanned 0 production files but target UI3D exists.\n"
                "Scan dirs: ${_scan_dirs}\n"
                "A silently skipped gate is worse than a failing one. Fix the scan path or "
                "explicitly disable the gate.")
        else()
            message(STATUS
                "[scene write surface 3d] skipped: UI3D target is not built and 0 files were scanned")
        endif()
        return()
    endif()

    set(_violations "")
    foreach(_file IN LISTS _files)
        file(READ "${_file}" _content)
        file(RELATIVE_PATH _rel "${_root}" "${_file}")

        # 1) 引擎内写网关：白名单外的生产文件一律不得引用
        if(_content MATCHES "SceneWriteGateway3D")
            if(NOT _rel IN_LIST _gateway_allowlist)
                list(APPEND _violations "${_rel}: uses SceneWriteGateway3D outside the allowlist (structure writes must go through SceneEditService3D or the undo commands)")
            endif()
            continue()
        endif()

        # 2) UI/3D 生产代码直接调用结构写接口
        if(_rel MATCHES "^UI/3D/Src/" AND
           _content MATCHES "->[ \t]*(addEntity|addEntities|removeEntity|deleteEntities|extractEntities)[ \t]*\\(")
            list(APPEND _violations "${_rel}: calls a private SceneManager3D structure-write entry")
            continue()
        endif()

        # 3) Main 里的 3D 场景句柄直接调用结构写接口
        if(_content MATCHES "(m_sceneManager3D|sceneManager3D|scene3D)[ \t]*->[ \t]*(addEntity|addEntities|removeEntity|deleteEntities|extractEntities)[ \t]*\\(")
            list(APPEND _violations "${_rel}: calls a private SceneManager3D structure-write entry")
            continue()
        endif()

        # 4) *Direct 旁路只允许白名单文件
        if(_content MATCHES
           "SceneEditService3D[ \t]*::[ \t]*(addEntityDirect|addEntitiesDirect|removeEntityDirect|deleteEntitiesDirect|extractEntitiesDirect|replaceSceneDirect)")
            if(NOT _rel IN_LIST _direct_allowlist)
                list(APPEND _violations
                    "${_rel}: calls SceneEditService3D::*Direct outside the allowlist (no undo record)")
            endif()
            continue()
        endif()
    endforeach()

    if(_violations)
        string(REPLACE ";" "\n  - " _violation_text "${_violations}")
        message(FATAL_ERROR
            "Production code must not write the 3D scene structure directly "
            "(SceneEditService3D is the only write entry point).\n"
            "Violating files:\n  - ${_violation_text}\n"
            "Fix: call SceneEditService3D (undoable methods for user edits, "
            "*Direct statics only for allowlisted document-level / operation-internal paths).")
    endif()

    message(STATUS
        "[scene write surface 3d] production code does not bypass the write entry "
        "(scanned=${_file_count} skipped=${_skipped_count} allowlist=${_allowlist_count})\n"
        "[scene write surface 3d] gateway allowlist: ${_gateway_allowlist}\n"
        "[scene write surface 3d] *Direct allowlist: ${_direct_allowlist}")
endfunction()
