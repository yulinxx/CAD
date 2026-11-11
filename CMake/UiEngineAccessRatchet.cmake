# ============================================================================
# UI → Engine include 层棘轮校验（CI / 本地 configure 时自动执行）
# ----------------------------------------------------------------------------
# 为什么需要这道校验
#   UiEngineAccess 是**链接层**的单一收口点（规则 3 强制，见 CheckDependencies.cmake）。
#   include 层从未收口：UI 大量直接 #include "Engine2D/..." / "Engine3D/..." /
#   "Engine/..."，因为 UI 与 Engine 共享同一套领域模型（SceneManager / AprEntity /
#   Selection / 编辑服务），公共头也已经暴露 Engine 类型。把这块迁进转发头是
#   一次公共 API 治理，成本高、收益低，本仓库明确不做存量迁移。
#
#   但"不做迁移"不等于"随便增长"。这道棘轮把当前**依赖面**冻结成白名单：
#     - 现有调用方一行不用改；
#     - 新出现的 Engine 头依赖（新 API 面）会在 configure 阶段硬失败，
#       逼迫新代码优先走 UiEngineAccess/Include 下的转发头；
#     - 确需扩张时，必须在本文件同步加入白名单 —— 让依赖面扩张成为一次
#       显式、可 review 的决策，而不是悄悄长出来；
#     - 白名单条目不再被任何生产代码引用时同样 configure 失败 ——
#       保证这张表只减不增，失效豁免不会永久沉淀。
#
# 为什么按「去重后的头文件」而不是「include 次数」
#   - 依赖面由「依赖了哪些头」定义，不是由「在多少个文件里 include」定义。
#     纯代码搬移（把一个 .cpp 拆成多个）会让次数上涨但依赖面不变，按次数
#     统计会误报；按头文件集合统计则不受拆分影响。
#   - 反过来，换一个从未被依赖的 Engine 头（真正的依赖面扩张）一定会被抓到。
#
# 统计口径（与白名单必须一致）
#   - 路径：UI/Common、UI/2D、UI/3D 下所有 .cpp/.h/.mm；
#   - 排除：任意名为 Test 的目录（测试用例不计入门面约束）；
#   - 模式：#include "Engine2D/..." | "Engine3D/..." | "EngineCommon/..." |
#           "Engine/..."（后两者分别是 EngineCommon 与 Engine 兼容层的 include 前缀）。
#
# 与规则 3 的分工
#   - 规则 3（CheckDependencies.cmake）：禁止 UI 直接**链接** Engine，查 CMake 依赖图；
#   - 本校验：只查**源码 include 的头文件集合**，不改变任何调用方，只阻止依赖面扩张。
# ============================================================================

# 去掉注释与字面量 #if 0 分支，只保留真正参与编译的代码。
# 反向检查不能把注释掉的 include 当成"仍在使用"，否则失效豁免永远删不掉。
# 条件编译采取保守策略：除字面量 #if 0 外，#if/#ifdef/#ifndef 一律视为参与编译，
# #else/#elif 会重新放行 —— 宁可漏删，也不把仍在用的条目误报成未使用。
function(_apr_strip_dead_code _content _out_var)
    string(REPLACE "\n" ";" _lines "${_content}")

    set(_in_block_comment FALSE)
    set(_states "")
    set(_live_lines "")
    foreach(_line IN LISTS _lines)
        # 逐行剔除块注释，保留两个注释块之间的真实代码
        set(_keep "")
        set(_rest "${_line}")
        while(TRUE)
            if(_in_block_comment)
                string(FIND "${_rest}" "*/" _comment_end)
                if(_comment_end EQUAL -1)
                    set(_rest "")
                    break()
                endif()
                math(EXPR _comment_end "${_comment_end} + 2")
                string(SUBSTRING "${_rest}" ${_comment_end} -1 _rest)
                set(_in_block_comment FALSE)
            else()
                string(FIND "${_rest}" "/*" _comment_start)
                if(_comment_start EQUAL -1)
                    string(APPEND _keep "${_rest}")
                    break()
                endif()
                string(SUBSTRING "${_rest}" 0 ${_comment_start} _before)
                string(APPEND _keep "${_before}")
                math(EXPR _comment_start "${_comment_start} + 2")
                string(SUBSTRING "${_rest}" ${_comment_start} -1 _rest)
                set(_in_block_comment TRUE)
            endif()
        endwhile()
        string(REGEX REPLACE "//.*$" "" _line "${_keep}")
        string(REGEX REPLACE "\r$" "" _line "${_line}")

        if(_line MATCHES "^[ \t]*#[ \t]*if")
            if(_line MATCHES "^[ \t]*#[ \t]*if[ \t]+0[ \t]*$")
                list(APPEND _states 1)
            else()
                list(APPEND _states 0)
            endif()
        elseif(_line MATCHES "^[ \t]*#[ \t]*(else|elif)")
            list(LENGTH _states _state_count)
            if(_state_count GREATER 0)
                list(GET _states -1 _state)
                if(_state EQUAL 1)
                    # #if 0 的 #else/#elif 分支仍然参与编译
                    list(REMOVE_AT _states -1)
                    list(APPEND _states 0)
                endif()
            endif()
        elseif(_line MATCHES "^[ \t]*#[ \t]*endif")
            list(LENGTH _states _state_count)
            if(_state_count GREATER 0)
                list(REMOVE_AT _states -1)
            endif()
        else()
            set(_dead FALSE)
            foreach(_state IN LISTS _states)
                if(_state EQUAL 1)
                    set(_dead TRUE)
                endif()
            endforeach()
            if(NOT _dead)
                list(APPEND _live_lines "${_line}")
            endif()
        endif()
    endforeach()

    string(REPLACE ";" "\n" _live "${_live_lines}")
    set(${_out_var} "${_live}" PARENT_SCOPE)
endfunction()

function(apr_check_ui_engine_include_ratchet)
    set(_root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")

    # 基线 = 2026-11 存量快照（去重后的 Engine 头文件）。只允许删除，不允许新增。
    set(_allow_UI_Common
        "Engine/Edit/GeometryEditResult.h"
        "Engine/Parallel/EngineParallel.h"
        "Engine/Render/TessParams.h"
        "Engine/AprEntity/AprEntity.h"
        "Engine/Text/FontParameters.h"
        "Engine/Tool/IToolModel.h"
        "Engine2D/Core/GroupManager.h"
        "Engine2D/Core/SceneManager.h"
        "Engine2D/Edit/SceneEditService.h"
        "Engine2D/Geo/GeometryContext.h"
        "Engine2D/Geometry/Geo2DBoolean.h"
        "Engine2D/Geometry/PolygonAlgorithms.h"
        "Engine2D/AprEntity/AprGroup.h"
    )
    set(_allow_UI_2D
        "Engine/Edit/IUndoRedoManager.h"
        "Engine/EntityIdGenerator.h"
        "Engine/Layer/AprLayer.h"
        "Engine/Scene/ISceneContext.h"
        "Engine/Scene/SceneRenderContract.h"
        "Engine/AprEntity/EType.h"
        "Engine/AprEntity/AprEntity.h"
        "Engine/Text/FontManager.h"
        "Engine/Text/FontParameters.h"
        "Engine/TextItem.h"
        "Engine/Tool/IToolModel.h"
        "Engine/Tool/IToolStateMachine.h"
        "Engine2D/Cam/KerfCompensator.h"
        "Engine2D/Core/GroupManager.h"
        "Engine2D/Core/SceneManager.h"
        "Engine2D/Edit/ISceneAccess.h"
        "Engine2D/Edit/LayerEditService.h"
        "Engine2D/Edit/SceneAccessAdapters.h"
        "Engine2D/Edit/SceneEditService.h"
        "Engine2D/Edit/SceneUndoCommands.h"
        "Engine2D/Environment/SceneEnvLayer.h"
        "Engine2D/Environment/SceneEnvironment.h"
        "Engine2D/Geo/GeometryContext.h"
        "Engine2D/Interaction/GridSnapManager.h"
        "Engine2D/Interaction/HardwareProfileManager.h"
        "Engine2D/Interaction/ILayerManager.h"
        "Engine2D/Interaction/LayerManager.h"
        "Engine2D/Interaction/SnapEngine.h"
        "Engine2D/Machine/MachineFrame.h"
        "Engine2D/AprEntity/AprArc.h"
        "Engine2D/AprEntity/AprBezier.h"
        "Engine2D/AprEntity/AprBezier2.h"
        "Engine2D/AprEntity/AprCircle.h"
        "Engine2D/AprEntity/AprEllipse.h"
        "Engine2D/AprEntity/AprGroup.h"
        "Engine2D/AprEntity/AprImage.h"
        "Engine2D/AprEntity/AprLine.h"
        "Engine2D/AprEntity/AprNurbs.h"
        "Engine2D/AprEntity/AprPoint.h"
        "Engine2D/AprEntity/AprPolygon.h"
        "Engine2D/AprEntity/AprQRCode.h"
        "Engine2D/AprEntity/AprSmartLine.h"
        "Engine2D/AprEntity/AprText.h"
        "Engine2D/Text/TextConverter.h"
        "Engine2D/Text/TextGroupSnapshot.h"
    )
    set(_allow_UI_3D
        "Engine/Edit/IUndoRedoCommand.h"
        "Engine/Edit/IUndoRedoManager.h"
        "Engine/EntityIdGenerator.h"
        "Engine/EntityIdUtils.h"
        "Engine/Scene/SceneChangeSet.h"
        "Engine/Scene/SceneGeometryCollector.h"
        "Engine/Scene/SceneRenderContract.h"
        "Engine/SpatialIndex/ISpatialIndex.h"
        "Engine/Tool/IToolStateMachine.h"
        "Engine3D/Algorithm/Geo3DEdit.h"
        "Engine3D/Edit/SceneUndoCommands3D.h"
        "Engine3D/Geometry/Geo3DTypes.h"
        "Engine3D/Import/FioEntityConverter.h"
        "Engine3D/SceneManager3D.h"
        "Engine3D/Selection/SelectionManager3D.h"
        "Engine3D/AprEntity/AprMeshEntity.h"
        "Engine3D/Transform/ITransformer.h"
    )

    set(_violations "")
    set(_summary "")

    foreach(_module IN ITEMS "UI/Common" "UI/2D" "UI/3D")
        if(NOT IS_DIRECTORY "${_root}/${_module}")
            # 模块未检出（子模块/裁剪构建）时跳过，并在汇总里注明
            list(APPEND _summary "${_module}=skipped")
            continue()
        endif()

        if(_module STREQUAL "UI/Common")
            set(_allowed ${_allow_UI_Common})
        elseif(_module STREQUAL "UI/2D")
            set(_allowed ${_allow_UI_2D})
        else()
            set(_allowed ${_allow_UI_3D})
        endif()

        file(GLOB_RECURSE _files
            "${_root}/${_module}/*.cpp"
            "${_root}/${_module}/*.h"
            "${_root}/${_module}/*.mm"
        )
        list(FILTER _files EXCLUDE REGEX "[/\\\\]Test[/\\\\]")

        set(_found "")
        foreach(_f IN LISTS _files)
            file(READ "${_f}" _content)
            # 注释与 #if 0 里的 include 不算真实依赖；尖括号与引号两种写法都识别
            _apr_strip_dead_code("${_content}" _live_content)
            string(REGEX MATCHALL
                "#include[ \t]*[\"<](Engine2D|Engine3D|EngineCommon|Engine)/[^\">]+[\">]"
                _hits "${_live_content}")
            if(NOT _hits)
                continue()
            endif()
            file(RELATIVE_PATH _rel "${_root}" "${_f}")
            foreach(_hit IN LISTS _hits)
                string(REGEX REPLACE "^#include[ \t]*[\"<]" "" _hdr "${_hit}")
                string(REGEX REPLACE "[\">]$" "" _hdr "${_hdr}")
                list(APPEND _found "${_hdr}|${_rel}")
            endforeach()
        endforeach()

        # 去重后的依赖头集合（报告用）
        set(_headers "")
        foreach(_entry IN LISTS _found)
            string(REPLACE "|" ";" _p "${_entry}")
            list(GET _p 0 _hdr)
            list(APPEND _headers "${_hdr}")
        endforeach()
        list(REMOVE_DUPLICATES _headers)
        list(LENGTH _headers _header_count)
        list(APPEND _summary "${_module}=${_header_count} headers")

        # 反向检查：白名单条目必须仍被本模块真实代码引用。
        # 命中 0 次 = 依赖面已经收缩，对应豁免必须同步删除（只减不增）。
        set(_unused "")
        foreach(_hdr IN LISTS _allowed)
            if(NOT _hdr IN_LIST _headers)
                list(APPEND _unused "${_hdr}")
            endif()
        endforeach()
        if(_unused)
            string(REPLACE ";" ", " _unused_text "${_unused}")
            list(APPEND _violations
                "[${_module}] whitelist entries no longer used (delete them): ${_unused_text}")
        endif()

        # 逐条检查：不在白名单 = 依赖面扩张
        foreach(_hdr IN LISTS _headers)
            if(NOT _hdr IN_LIST _allowed)
                set(_where "")
                foreach(_entry IN LISTS _found)
                    string(REPLACE "|" ";" _p "${_entry}")
                    list(GET _p 0 _e_hdr)
                    list(GET _p 1 _e_rel)
                    if(_e_hdr STREQUAL _hdr)
                        list(APPEND _where "${_e_rel}")
                    endif()
                endforeach()
                list(REMOVE_DUPLICATES _where)
                string(REPLACE ";" ", " _where_text "${_where}")
                list(APPEND _violations
                    "[${_module}] new Engine header dependency \"${_hdr}\" (used by ${_where_text})")
            endif()
        endforeach()
    endforeach()

    string(REPLACE ";" "  " _summary_text "${_summary}")

    if(_violations)
        string(REPLACE ";" "\n    - " _msg "${_violations}")
        message(FATAL_ERROR
            "[apr_check_ui_engine_include_ratchet] UI-to-Engine include surface violation:\n    - ${_msg}\n"
            "  Fix with one of:\n"
            "    1) Use a forwarding header under UiEngineAccess/Include/UiEngineAccess/"
            " (adding a forwarding header is an explicit surface-expansion decision);\n"
            "    2) If the Engine header is genuinely required, add its path to the module"
            " whitelist in CMake/UiEngineAccessRatchet.cmake and explain why in the commit message;\n"
            "    3) Delete whitelist entries reported as no longer used.\n"
            "  Current stats: ${_summary_text}")
    endif()

    message(STATUS "[April] UI-to-Engine include ratchet: OK (${_summary_text}; whitelist-based surface, shrink-only)")
endfunction()
