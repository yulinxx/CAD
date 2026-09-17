# ============================================================================
# UiEngineAccess — UI 层访问 Engine 的唯一门面（INTERFACE target）
# ----------------------------------------------------------------------------
# 目的
#   把「UI → Engine」的依赖收口到一个可审计、可替换的目标，避免 UI 各模块
#   各自直接链接 EngineCommon/Engine2D/Engine3D/EnginePersistence。
#
# 为什么不是"接口抽象"
#   UI 与 Engine 共享同一套领域模型（SceneManager / SyEntity / Selection /
#   撤销命令等），无法用窄接口隔离（详见
#   Docs/01-当前架构/UI-Engine解耦迁移方案.md §1.2）。本门面只做"依赖收口"：
#   UI 的 365 处 #include "Engine*/..." 无需改动，包含目录由本目标传递。
#
# 规则（由 sanyi_check_dependency_directions 强制）
#   - UI/* 模块只允许链接 UiEngineAccess，禁止直接链接 Engine*。
#   - 依赖方向单向：UiEngineAccess → Engine*；Engine 不得反向依赖 UI。
# ============================================================================

function(sanyi_define_ui_engine_access)
    if(TARGET UiEngineAccess)
        return()
    endif()

    # 前置条件：Engine 目标必须已存在（调用方需先 add_subdirectory(Engine/*)）
    set(_engine_targets EngineCommon Engine2D Engine3D EnginePersistence)
    foreach(_t IN LISTS _engine_targets)
        if(NOT TARGET ${_t})
            message(FATAL_ERROR
                "[sanyi_define_ui_engine_access] 缺少目标 ${_t}；"
                "请在 add_subdirectory(Engine/*) 之后再调用本函数。")
        endif()
    endforeach()

    add_library(UiEngineAccess INTERFACE)

    # INTERFACE 链接：把 Engine 的公共头目录与库传递给 UI 消费者
    target_link_libraries(UiEngineAccess INTERFACE
        ${_engine_targets}
    )

    set_target_properties(UiEngineAccess PROPERTIES
        FOLDER "Engine"
        DESCRIPTION "Single access facade: UI -> Engine (dependency choke point)"
    )

    message(STATUS "[UiEngineAccess] UI → Engine 唯一访问门面已定义")
endfunction()
