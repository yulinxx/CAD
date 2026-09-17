# ============================================================================
# 依赖方向校验（CI / 本地 configure 时自动执行）
# ----------------------------------------------------------------------------
# 强制三条分层规则，防止"假解耦"重新长出来：
#   1. Engine*  不得依赖 UI* / Render* / Main / UiEngineAccess
#   2. RenderX / RenderBridge 不得依赖 UI* / Main
#   3. UI* 不得直接依赖 Engine*（只能经 UiEngineAccess 门面）
#
# 只检查**直接**链接（LINK_LIBRARIES），不做传递闭包 —— 传递依赖由 CMake
# 目标系统保证，直接依赖才是人为写错的地方。
#
# 用法：在所有 add_subdirectory 完成后调用 sanyi_check_dependency_directions()。
# ============================================================================

function(sanyi_check_dependency_directions)
    set(_engine_targets  EngineCommon Engine2D Engine3D EnginePersistence)
    set(_render_targets  RenderX RenderBridge)
    set(_ui_targets      UICommon UI2D UI3D)
    set(_app_targets     SanYiCAD)

    set(_violations "")

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

    # 规则 3：UI 不得绕过门面直接依赖 Engine
    set(_engine_direct "^(EngineCommon|Engine2D|Engine3D|EnginePersistence)$")
    foreach(_t IN LISTS _ui_targets)
        if(TARGET ${_t})
            get_target_property(_libs ${_t} LINK_LIBRARIES)
            foreach(_dep IN LISTS _libs)
                if(_dep MATCHES "${_engine_direct}")
                    list(APPEND _violations
                        "UI 目标 ${_t} 直接链接了 ${_dep}；应改为链接 UiEngineAccess")
                endif()
            endforeach()
        endif()
    endforeach()

    if(_violations)
        string(REPLACE ";" "\n    - " _msg "${_violations}")
        message(FATAL_ERROR
            "[sanyi_check_dependency_directions] 依赖方向违规：\n    - ${_msg}\n"
            "  规则见 Docs/01-当前架构/UI-Engine解耦迁移方案.md")
    endif()

    message(STATUS "[SanYi] 依赖方向校验: OK "
        "(Engine 不依赖 UI/Render；Render 不依赖 UI；UI 经 UiEngineAccess 访问 Engine)")
endfunction()
