# ============================================================================
# 依赖方向校验（CI / 本地 configure 时自动执行）
# ----------------------------------------------------------------------------
# 强制分层规则，防止"假解耦"重新长出来：
#   1. Engine*  不得依赖 UI* / Render* / Main / UiEngineAccess
#   2. RenderX / RenderBridge 不得依赖 UI* / Main
#   3. UI* 不得直接**链接** Engine*（链接层只能经 UiEngineAccess 门面）
#   4. UI* 不得直接依赖 RenderX（只能通过 RenderAbstraction 接口）
#   5. UI* 的 include 目录不得引入 RenderX 头路径（直调 rx* 的能力来源）
#   6. UI* 不得经**传递闭包**到达 RenderX
#
# 规则 1~4 只查**直接**链接（LINK_LIBRARIES）：直接依赖才是人为写错的地方。
#
# ⚠ 规则 3 的边界（务必读清，避免误解为"include 层已收口"）：
#   UiEngineAccess 是**链接层**的单一收口点。include 层并未收口：
#   - 转发头（UiEngineAccess/Include/UiEngineAccess/*.h）只覆盖 Engine2D 的
#     算法 / 填充 / 离散化子集；
#   - 场景 / 实体 / 选择 / 编辑等 API 仍由 UI 直接
#     #include "Engine2D/..." / "Engine3D/..." / "Engine/..."；
#   - UI 的公共头也直接暴露 Engine 类型（见 Docs/01-当前架构/
#     UI-Engine解耦迁移方案.md §2.1），这是"共享领域模型"的既定取舍，
#     不是待修缺陷。
#   include 层不做存量迁移，但禁止依赖面继续扩张：见
#   CMake/UiEngineAccessRatchet.cmake（按"去重后的 Engine 头文件"白名单校验）。
#
# 规则 5~6 是针对"假解耦"的补漏。规则 4 只管直接链接，于是出现了一条绕过路径：
# UI 只链 RenderBridge → RenderBridge 用 PUBLIC 转发 RenderX → UI 照样拿到
# RenderX 的头路径与符号，反手直调 rx* API。此时规则 4 判过，耦合却一点没少。
#   规则 5 抓根因：UI 能调 rx*，是因为它自己的 include 目录里有 renderx.h 的路径。
#   规则 6 抓结构：UI 的可达依赖里仍有 RenderX，提示"转发链还没收口"。
#
# 这两条现在默认硬失败（APR_STRICT_RENDER_ISOLATION=ON）：收口已经完成，
# 规则通过，把它冻结住才能防止耦合重新长回来。真需要临时放行时显式传
# -DAPR_STRICT_RENDER_ISOLATION=OFF。
#
# 用法：在所有 add_subdirectory 完成后调用 apr_check_dependency_directions()。
# ============================================================================

# 见文件头「规则 5~6」：ON 时违规升级为 configure 失败，OFF 时只告警
option(APR_STRICT_RENDER_ISOLATION
    "Promote UI-to-RenderX isolation warnings to configure failures" ON)

# ON 时「应被校验的目标不存在」也算失败，供 CI 全模块构建使用。
# 本地按 BUILD_xxx 关掉部分模块时保持 OFF：目标不存在是正常的，
# 但结果里必须如实说明有哪些规则被跳过了（见函数末尾）。
option(APR_STRICT_DEPENDENCY_AUDIT
    "Promote missing dependency-audit targets to configure failures" OFF)

# 收集某目标**对下游可见**的可达依赖（含传递闭包），结果写入 ${_out_var}。
#
# 关键点：判据是 INTERFACE_LINK_LIBRARIES，不是 LINK_LIBRARIES。
# 共享库自己的私有依赖同样出现在 LINK_LIBRARIES 里（它确实要链进去），
# 但**不会**出现在 INTERFACE_LINK_LIBRARIES —— 后者才决定下游拿到什么
# （链接行与 include 目录）。用 LINK_LIBRARIES 判定的话，「已收口」和
# 「PUBLIC 转发」两种情形看起来一样，这条规则就失去了判据意义。
#
# $<LINK_ONLY:...> 仍然滤掉：静态库会用它把私有依赖记进接口里。
function(_apr_collect_reachable_targets _root _out_var)
    set(_pending "${_root}")
    set(_seen "")
    while(_pending)
        list(POP_FRONT _pending _cur)
        if(NOT TARGET ${_cur})
            continue()
        endif()
        if(_cur IN_LIST _seen)
            continue()
        endif()
        list(APPEND _seen "${_cur}")
        get_target_property(_ideps ${_cur} INTERFACE_LINK_LIBRARIES)
        if(_ideps)
            list(FILTER _ideps EXCLUDE REGEX "^\\$<LINK_ONLY:")
            list(APPEND _pending ${_ideps})
        endif()
    endwhile()
    set(${_out_var} "${_seen}" PARENT_SCOPE)
endfunction()

function(apr_check_dependency_directions)
    set(_engine_targets  EngineCommon Engine2D Engine3D EnginePersistence)
    set(_render_targets  RenderX RenderBridge)
    set(_ui_targets      UICommon UI2D UI3D)
    set(_app_targets     April)
    set(_abstraction_targets RenderAbstraction)

    set(_violations "")

    # 每条规则都被 if(TARGET ...) 包着：模块被 BUILD_xxx=OFF 关掉时，
    # 整条规则会静默跳过。旧实现末尾无条件打印「OK」，等于只跑了半套规则
    # 却宣称全绿。这里先把本轮没参与校验的目标如实记录下来。
    set(_all_checked_targets
        ${_engine_targets} ${_render_targets} ${_ui_targets}
        ${_app_targets} ${_abstraction_targets}
        CAM Utility Log License CrashHandler Network Hardware Vision Nesting
        Raster PythonHost AprPyBindCore Engraving FileIO GeoModelCore)

    # 被 BUILD_xxx=OFF 主动关掉的模块不算“漏审”：比如 CI 为控制时长关掉
    # HARDWARE/VISION/ENGRAVING/GEOMODELCORE/PYTHON，目标本就不该存在。
    # 只有选项为 ON 却找不到目标，才是真正的审计缺口。
    set(_audit_option_guards
        "Hardware=BUILD_HARDWARE"
        "Network=BUILD_NETWORK"
        "Vision=BUILD_VISION"
        "Nesting=BUILD_NESTING"
        "Raster=BUILD_RASTER"
        "PythonHost=BUILD_PYTHON"
        "AprPyBindCore=APR_PYBIND_CORE"
        "Engraving=BUILD_ENGRAVING"
        "GeoModelCore=BUILD_GEOMODELCORE"
        "CrashHandler=BUILD_CRASHHANDLER"
        "CAM=BUILD_CAM")
    set(_intentionally_disabled "")
    foreach(_pair IN LISTS _audit_option_guards)
        string(REPLACE "=" ";" _kv "${_pair}")
        list(GET _kv 0 _guard_target)
        list(GET _kv 1 _guard_option)
        if(DEFINED ${_guard_option} AND NOT ${_guard_option})
            list(APPEND _intentionally_disabled "${_guard_target}")
        endif()
    endforeach()
    # PyBindCore 还要看 PythonHost 集成开关（两者同时 ON 才会生成模块目标）
    if(DEFINED APR_PYTHON_HOST AND NOT APR_PYTHON_HOST)
        list(APPEND _intentionally_disabled AprPyBindCore)
    endif()

    set(_missing_targets "")
    foreach(_t IN LISTS _all_checked_targets)
        if(NOT TARGET ${_t} AND NOT _t IN_LIST _intentionally_disabled)
            list(APPEND _missing_targets "${_t}")
        endif()
    endforeach()
    if(_missing_targets)
        list(REMOVE_DUPLICATES _missing_targets)
    endif()

    # 规则 1：Engine 不得依赖上层
    set(_engine_forbidden "^(UICommon|UI2D|UI3D|RenderX|RenderBridge|April|UiEngineAccess)$")
    foreach(_t IN LISTS _engine_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_engine_forbidden}")
                    list(APPEND _violations
                        "Engine target ${_t} depends on upper-layer ${_dep}")
                endif()
            endforeach()
        endif()
    endforeach()

    # 规则 2：Render 不得依赖 UI / Main
    set(_render_forbidden "^(UICommon|UI2D|UI3D|April)$")
    foreach(_t IN LISTS _render_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_render_forbidden}")
                    list(APPEND _violations
                        "Render target ${_t} depends on upper-layer ${_dep}")
                endif()
            endforeach()
        endif()
    endforeach()

    # 规则 3：UI 不得直接**链接** Engine（链接层必经 UiEngineAccess 门面）。
    # 注意：本规则不检查 include 层。UI 仍有大量 #include "Engine*/..." 直引
    # 场景/实体/编辑头，这是既定取舍；include 层只靠棘轮禁止增长
    # （CMake/UiEngineAccessRatchet.cmake），不要求存量迁移。
    set(_engine_direct "^(EngineCommon|Engine2D|Engine3D|EnginePersistence)$")
    foreach(_t IN LISTS _ui_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_engine_direct}")
                    list(APPEND _violations
                        "UI target ${_t} links ${_dep} directly (link UiEngineAccess instead)")
                endif()
            endforeach()
        endif()
    endforeach()

    # 规则 4：UI 不得直接依赖 RenderX（必须通过 RenderAbstraction）
    set(_render_direct "^(RenderX)$")
    foreach(_t IN LISTS _ui_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_render_direct}")
                    list(APPEND _violations
                        "UI target ${_t} links ${_dep} directly (link RenderAbstraction instead)")
                endif()
            endforeach()
        endif()
    endforeach()

    # 规则 4.5：CAM 不得依赖 Engine2D / UI* / Render* / Main
    # CAM 的唯一职责是对 ToolpathJobInput（中性 POD）做工艺编译，
    # 任何对 Engine2D/UI/Render/Hardware 头的 include 都说明耦合破了
    if(TARGET CAM)
        set(_cam_forbidden "^(EngineCommon|Engine2D|Engine3D|EnginePersistence|UICommon|UI2D|UI3D|RenderX|RenderBridge|RenderAbstraction|April|UiEngineAccess)$")
        get_target_property(_libs CAM LINK_LIBRARIES)
        if(_libs)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_cam_forbidden}")
                    list(APPEND _violations
                        "CAM target links upper-layer module ${_dep} (CAM may only depend on Utility/Log/Engraving(PRIVATE)/Hardware(PRIVATE))")
                endif()
            endforeach()
        endif()
        # 检查 INTERFACE_INCLUDE_DIRS 是否泄漏上层头路径（比 LINK_LIBRARIES 更严格）
        get_target_property(_cam_includes CAM INTERFACE_INCLUDE_DIRS)
        if(_cam_includes)
            foreach(_inc IN LISTS _cam_includes)
                if(_inc MATCHES "[/\\\\](Engine2D|EngineCommon|UICommon|UI2D|UI3D|RenderX|RenderBridge|Hardware)[/\\\\]Include")
                    list(APPEND _violations
                        "CAM INTERFACE_INCLUDE_DIRS leaks upper-layer include path ${_inc}")
                endif()
            endforeach()
        endif()
    endif()

    # 规则 7：基础 / 服务模块不得依赖上层（UI / Render / Main / 门面）。
    # 覆盖此前不在规则内的 14 个共享库目标。这里只禁止**向上**依赖，
    # 不重新定义它们之间的既有向下依赖（例如 FileIO 依赖 Engine*/GeoModelCore、
    # Engraving 依赖 Engine3D 都是既定设计，不在此处判违规）。
    set(_base_targets
        Utility Log License CrashHandler Network Hardware Vision Nesting
        Raster PythonHost AprPyBindCore Engraving FileIO GeoModelCore)
    set(_base_forbidden
        "^(UICommon|UI2D|UI3D|RenderX|RenderBridge|RenderAbstraction|April|UiEngineAccess)$")
    foreach(_t IN LISTS _base_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            if(_libs)
                foreach(_dep IN LISTS _libs)
                    if(_dep MATCHES "${_base_forbidden}")
                        list(APPEND _violations
                            "Base/service target ${_t} depends on upper-layer ${_dep}")
                    endif()
                endforeach()
            endif()
            # 公开头路径同样不能把上层头带给下游（比 LINK_LIBRARIES 更严格）
            get_target_property(_incs ${_t} INTERFACE_INCLUDE_DIRS)
            if(_incs)
                foreach(_inc IN LISTS _incs)
                    if(_inc MATCHES "[/\\\\](UICommon|UI2D|UI3D|Renderx|RenderBridge|Main)[/\\\\](Include|include)")
                        list(APPEND _violations
                            "Base/service target ${_t} leaks upper-layer include path ${_inc}")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()

    # 规则 8：Engine / Render 不得反向依赖基础服务或绑定层（防止循环依赖）。
    # Utility/Log 仍是允许的底座，不在此列。
    set(_lower_forbidden
        "^(FileIO|Hardware|Vision|Nesting|Raster|PythonHost|AprPyBindCore|Engraving|Network|CrashHandler|License|GeoModelCore)$")
    foreach(_t IN LISTS _engine_targets _render_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            if(_libs)
                foreach(_dep IN LISTS _libs)
                    if(_dep MATCHES "${_lower_forbidden}")
                        list(APPEND _violations
                            "Lower-layer target ${_t} depends on base/service module ${_dep} (dependency cycle risk)")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()

    # 规则 5：UI 不得在自己的 include 目录里引入 RenderX 头路径。
    # 这是"UI 能直接调 rx*"的能力来源——去掉它，直调那批代码会立刻编译失败，
    # 于是耦合无法悄悄长回来。允许残留告警：收口是渐进的。
    set(_advisory "")
    foreach(_t IN LISTS _ui_targets)
        if(TARGET ${_t})
            get_target_property(_incs ${_t} INCLUDE_DIRECTORIES)
            if(_incs)
                foreach(_inc IN LISTS _incs)
                    if(_inc MATCHES "[/\\\\][Rr]enderx[/\\\\]include[/\\\\]?$")
                        list(APPEND _advisory
                            "UI target ${_t} has a RenderX include path (${_inc}): "
                            "its .cpp files still call rx* APIs directly (route them through RenderAbstraction)")
                    endif()
                endforeach()
            endif()
        endif()
    endforeach()

    # 规则 6：UI 不得经传递闭包到达 RenderX。
    # 直接链接层看不出来（UI 只链 RenderBridge），但 RenderBridge 以 PUBLIC 转发
    # RenderX，链路实际是通的。收口方式：UI 停止直调后，把 RenderBridge 对
    # RenderX 的转发由 PUBLIC 收到 PRIVATE。
    foreach(_t IN LISTS _ui_targets)
        if(TARGET ${_t})
            _apr_collect_reachable_targets(${_t} _reachable)
            if("RenderX" IN_LIST _reachable)
                list(APPEND _advisory
                    "UI target ${_t} can still reach RenderX through transitive dependencies (chain: ${_t} -> RenderBridge -> RenderX)")
            endif()
        endif()
    endforeach()

    # 告警与硬失败的排序：先报必错项，再报隔离项，最后一起退出
    if(_advisory)
        list(REMOVE_DUPLICATES _advisory)
        string(REPLACE ";" "\n    - " _advisory_msg "${_advisory}")
        if(APR_STRICT_RENDER_ISOLATION)
            list(APPEND _violations "UI/RenderX isolation is not closed (APR_STRICT_RENDER_ISOLATION=ON):\n    - ${_advisory_msg}")
        else()
            message(WARNING
                "[apr_check_dependency_directions] UI/RenderX isolation is not closed (warning only):\n    - ${_advisory_msg}\n"
                "  Once closed, pass -DAPR_STRICT_RENDER_ISOLATION=ON to make this a hard failure")
        endif()
    endif()

    set(_skip_note "")
    if(_missing_targets)
        string(REPLACE ";" ", " _missing_msg "${_missing_targets}")
        set(_skip_note "; targets not audited: ${_missing_msg}")
        if(APR_STRICT_DEPENDENCY_AUDIT)
            list(APPEND _violations
                "Dependency-audit targets are missing (APR_STRICT_DEPENDENCY_AUDIT=ON): ${_missing_msg}")
        endif()
    endif()

    set(_audited_count 0)
    foreach(_t IN LISTS _all_checked_targets)
        if(TARGET ${_t})
            math(EXPR _audited_count "${_audited_count} + 1")
        endif()
    endforeach()

    if(_violations)
        string(REPLACE ";" "\n    - " _msg "${_violations}")
        message(FATAL_ERROR
            "[apr_check_dependency_directions] dependency direction violation:\n    - ${_msg}\n"
            "  See Docs: UI-Engine decoupling migration plan (01-architecture)")
    endif()

    if(_advisory)
        # 分层规则都过了，但 UI 仍能直调 RenderX —— 不谎报"完全解耦"
        message(STATUS "[April] dependency directions: OK (${_audited_count} targets audited; layering rules pass; UI/RenderX isolation is still open, see warnings above${_skip_note})")
    else()
        message(STATUS "[April] dependency directions: OK "
            "(${_audited_count} targets audited; "
            "Engine does not depend on UI/Render; Render does not depend on UI; "
            "UI reaches Engine only through UiEngineAccess at link level (includes remain open, covered by the ratchet); "
            "UI reaches rendering through RenderAbstraction${_skip_note})")
    endif()
endfunction()
