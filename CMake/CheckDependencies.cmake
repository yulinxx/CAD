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
# 这两条现在默认硬失败（SANYI_STRICT_RENDER_ISOLATION=ON）：收口已经完成，
# 规则通过，把它冻结住才能防止耦合重新长回来。真需要临时放行时显式传
# -DSANYI_STRICT_RENDER_ISOLATION=OFF。
#
# 用法：在所有 add_subdirectory 完成后调用 sanyi_check_dependency_directions()。
# ============================================================================

# 见文件头「规则 5~6」：ON 时违规升级为 configure 失败，OFF 时只告警
option(SANYI_STRICT_RENDER_ISOLATION
    "把 UI 直连 RenderX 的隔离告警升级为 configure 失败" ON)

# ON 时「应被校验的目标不存在」也算失败，供 CI 全模块构建使用。
# 本地按 BUILD_xxx 关掉部分模块时保持 OFF：目标不存在是正常的，
# 但结果里必须如实说明有哪些规则被跳过了（见函数末尾）。
option(SANYI_STRICT_DEPENDENCY_AUDIT
    "依赖方向校验所需目标缺失时升级为 configure 失败" OFF)

# 收集某目标**对下游可见**的可达依赖（含传递闭包），结果写入 ${_out_var}。
#
# 关键点：判据是 INTERFACE_LINK_LIBRARIES，不是 LINK_LIBRARIES。
# 共享库自己的私有依赖同样出现在 LINK_LIBRARIES 里（它确实要链进去），
# 但**不会**出现在 INTERFACE_LINK_LIBRARIES —— 后者才决定下游拿到什么
# （链接行与 include 目录）。用 LINK_LIBRARIES 判定的话，「已收口」和
# 「PUBLIC 转发」两种情形看起来一样，这条规则就失去了判据意义。
#
# $<LINK_ONLY:...> 仍然滤掉：静态库会用它把私有依赖记进接口里。
function(_sanyi_collect_reachable_targets _root _out_var)
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

function(sanyi_check_dependency_directions)
    set(_engine_targets  EngineCommon Engine2D Engine3D EnginePersistence)
    set(_render_targets  RenderX RenderBridge)
    set(_ui_targets      UICommon UI2D UI3D)
    set(_app_targets     SanYiCAD)
    set(_abstraction_targets RenderAbstraction)

    set(_violations "")

    # 每条规则都被 if(TARGET ...) 包着：模块被 BUILD_xxx=OFF 关掉时，
    # 整条规则会静默跳过。旧实现末尾无条件打印「OK」，等于只跑了半套规则
    # 却宣称全绿。这里先把本轮没参与校验的目标如实记录下来。
    set(_all_checked_targets
        ${_engine_targets} ${_render_targets} ${_ui_targets}
        ${_app_targets} ${_abstraction_targets} CAM)
    set(_missing_targets "")
    foreach(_t IN LISTS _all_checked_targets)
        if(NOT TARGET ${_t})
            list(APPEND _missing_targets "${_t}")
        endif()
    endforeach()
    if(_missing_targets)
        list(REMOVE_DUPLICATES _missing_targets)
    endif()

    # 规则 1：Engine 不得依赖上层
    set(_engine_forbidden "^(UICommon|UI2D|UI3D|RenderX|RenderBridge|SanYiCAD|UiEngineAccess)$")
    foreach(_t IN LISTS _engine_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_engine_forbidden}")
                    list(APPEND _violations
                        "Engine 目标 ${_t} 依赖了上层 ${_dep}")
                endif()
            endforeach()
        endif()
    endforeach()

    # 规则 2：Render 不得依赖 UI / Main
    set(_render_forbidden "^(UICommon|UI2D|UI3D|SanYiCAD)$")
    foreach(_t IN LISTS _render_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_render_forbidden}")
                    list(APPEND _violations
                        "Render 目标 ${_t} 依赖了上层 ${_dep}")
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
                        "UI 目标 ${_t} 直接链接了 ${_dep}；链接层应改为链接 UiEngineAccess")
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
                        "UI 目标 ${_t} 直接链接了 ${_dep}；应改为链接 RenderAbstraction")
                endif()
            endforeach()
        endif()
    endforeach()

    # 规则 4.5：CAM 不得依赖 Engine2D / UI* / Render* / Main
    # CAM 的唯一职责是对 ToolpathJobInput（中性 POD）做工艺编译，
    # 任何对 Engine2D/UI/Render/Hardware 头的 include 都说明耦合破了
    if(TARGET CAM)
        set(_cam_forbidden "^(EngineCommon|Engine2D|Engine3D|EnginePersistence|UICommon|UI2D|UI3D|RenderX|RenderBridge|RenderAbstraction|SanYiCAD|UiEngineAccess)$")
        get_target_property(_libs CAM LINK_LIBRARIES)
        if(_libs)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_cam_forbidden}")
                    list(APPEND _violations
                        "CAM 目标链接了上层模块 ${_dep}；CAM 只能依赖 Utility/Log/Engraving(PRIVATE)/Hardware(PRIVATE)")
                endif()
            endforeach()
        endif()
        # 检查 INTERFACE_INCLUDE_DIRS 是否泄漏上层头路径（比 LINK_LIBRARIES 更严格）
        get_target_property(_cam_includes CAM INTERFACE_INCLUDE_DIRS)
        if(_cam_includes)
            foreach(_inc IN LISTS _cam_includes)
                if(_inc MATCHES "[/\\\\](Engine2D|EngineCommon|UICommon|UI2D|UI3D|RenderX|RenderBridge|Hardware)[/\\\\]Include")
                    list(APPEND _violations
                        "CAM 的 INTERFACE_INCLUDE_DIRS 泄漏了上层头路径 ${_inc}")
                endif()
            endforeach()
        endif()
    endif()

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
                            "UI 目标 ${_t} 的 include 目录含 RenderX 头路径（${_inc}）："
                            "其 .cpp 仍在直调 rx* API，应收口到 RenderAbstraction 接口")
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
            _sanyi_collect_reachable_targets(${_t} _reachable)
            if("RenderX" IN_LIST _reachable)
                list(APPEND _advisory
                    "UI 目标 ${_t} 经传递依赖仍可到达 RenderX（链路：${_t} → RenderBridge → RenderX）")
            endif()
        endif()
    endforeach()

    # 告警与硬失败的排序：先报必错项，再报隔离项，最后一起退出
    if(_advisory)
        list(REMOVE_DUPLICATES _advisory)
        string(REPLACE ";" "\n    - " _advisory_msg "${_advisory}")
        if(SANYI_STRICT_RENDER_ISOLATION)
            list(APPEND _violations "UI/RenderX 隔离未收口（SANYI_STRICT_RENDER_ISOLATION=ON）：\n    - ${_advisory_msg}")
        else()
            message(WARNING
                "[sanyi_check_dependency_directions] UI/RenderX 隔离尚未收口（当前只告警）：\n    - ${_advisory_msg}\n"
                "  收口完成后可用 -DSANYI_STRICT_RENDER_ISOLATION=ON 把本项升级为硬失败")
        endif()
    endif()

    set(_skip_note "")
    if(_missing_targets)
        string(REPLACE ";" ", " _missing_msg "${_missing_targets}")
        set(_skip_note "；未参与校验的目标: ${_missing_msg}")
        if(SANYI_STRICT_DEPENDENCY_AUDIT)
            list(APPEND _violations
                "依赖方向校验所需目标缺失（SANYI_STRICT_DEPENDENCY_AUDIT=ON）：${_missing_msg}")
        endif()
    endif()

    if(_violations)
        string(REPLACE ";" "\n    - " _msg "${_violations}")
        message(FATAL_ERROR
            "[sanyi_check_dependency_directions] 依赖方向违规：\n    - ${_msg}\n"
            "  规则见 Docs/01-当前架构/UI-Engine解耦迁移方案.md")
    endif()

    if(_advisory)
        # 分层规则都过了，但 UI 仍能直调 RenderX —— 不谎报"完全解耦"
        message(STATUS "[SanYi] 依赖方向校验: OK（分层规则通过；UI/RenderX 隔离未收口，见上方告警${_skip_note}）")
    else()
        message(STATUS "[SanYi] 依赖方向校验: OK "
            "(Engine 不依赖 UI/Render；Render 不依赖 UI；"
            "UI 仅在链接层经 UiEngineAccess 访问 Engine（include 层未收口，另有棘轮校验）；"
            "UI 通过 RenderAbstraction 访问渲染${_skip_note})")
    endif()
endfunction()
