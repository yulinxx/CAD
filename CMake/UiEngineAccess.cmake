# ============================================================================
# UiEngineAccess — UI 层访问 Engine 的唯一门面（INTERFACE target）
# ----------------------------------------------------------------------------
# 目的
#   把「UI → Engine」的依赖收口到一个可审计、可替换的目标，避免 UI 各模块
#   各自直接链接 EngineCommon/Engine2D/Engine3D/EnginePersistence。
#
# 为什么不是"接口抽象"
#   UI 与 Engine 共享同一套领域模型（SceneManager / AprEntity / Selection /
#   撤销命令等），无法用窄接口隔离（详见
#   Docs/01-当前架构/UI-Engine解耦迁移方案.md §1.2）。
#
# 门面的实际覆盖范围（不要误读为"include 层已收口"）
#   - 链接层：这是**唯一**收口点。UI/* 只链本目标，由它向下传递 Engine*。
#   - include 层：只部分收口。UiEngineAccess/Include 下的转发头仅覆盖
#     Engine2D 的**算法 / 填充 / 离散化**子集；场景 / 实体 / 选择 / 编辑等
#     API 仍由 UI 直接 #include "Engine2D/..." / "Engine3D/..." / "Engine/..."。
#     这块不做存量迁移（公共头已暴露 Engine 类型，隐藏需另立公共 API 治理），
#     但禁止继续增长，由 CMake/UiEngineAccessRatchet.cmake 的棘轮校验拦截。
#
# 规则（由 apr_check_dependency_directions 强制）
#   - UI/* 模块只允许链接 UiEngineAccess，禁止直接链接 Engine*（链接层）。
#   - 依赖方向单向：UiEngineAccess → Engine*；Engine 不得反向依赖 UI。
# ============================================================================

function(apr_define_ui_engine_access)
    if(TARGET UiEngineAccess)
        return()
    endif()

    # 前置条件：Engine 目标必须已存在（调用方需先 add_subdirectory(Engine/*)）
    set(_engine_targets EngineCommon Engine2D Engine3D EnginePersistence)
    foreach(_t IN LISTS _engine_targets)
        if(NOT TARGET ${_t})
            message(FATAL_ERROR
                "[apr_define_ui_engine_access] missing target ${_t}; "
                "call this function after add_subdirectory(Engine/*).")
        endif()
    endforeach()

    add_library(UiEngineAccess INTERFACE)

    # 门面转发头：为 UI 提供 Engine2D **算法 / 填充 / 离散化** 子集的稳定
    # include 路径（UI 代码 include "UiEngineAccess/Xxx.h"）。
    # 注意：这不覆盖场景 / 实体 / 选择 / 编辑 API —— 那部分 UI 仍直接
    # include "Engine2D/..." / "Engine3D/..." / "Engine/"。转发头清单是
    # "允许新增的 include 收口面"，不是"UI 可触达的全部 Engine 面"。
    target_include_directories(UiEngineAccess INTERFACE
        "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../UiEngineAccess/Include"
    )

    # INTERFACE 链接：把 Engine 的公共头目录与库传递给 UI 消费者
    target_link_libraries(UiEngineAccess INTERFACE
        ${_engine_targets}
    )

    set_target_properties(UiEngineAccess PROPERTIES
        FOLDER "Engine"
        DESCRIPTION "Single access facade: UI -> Engine (dependency choke point)"
    )

    message(STATUS "[UiEngineAccess] UI -> Engine single access facade defined (link chokepoint)")
endfunction()
