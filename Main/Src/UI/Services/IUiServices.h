#pragma once

/**
 * @file IUiServices.h
 * @brief UI 服务窄接口 — 仅暴露已有抽象句柄
 *
 * 定义消费者应依赖的最小抽象集。当前暴露：
 * - getSelectionService()（选择状态查询/操作）
 * - getUndoManager()（撤销/重做栈）
 * - getInteractionDispatcher()（交互式命令生命周期）
 *
 * 需要具体服务（如 SceneDocument2D*、LayerManager* 等）的消费者，
 * 应通过独立参数注入，而非依赖此接口。
 *
 * @section 设计依据
 * UiServices.h 聚合了 16 个服务指针，违反"UI 只保留入口、交互和状态同步"原则。
 * 本接口作为迁移第一步，让消费者逐步从 UiServices 依赖转向 IUiServices。
 */

class ISelectionService;
class IUndoRedoManager;
class IInteractionDispatcher;

class IUiServices
{
public:
    virtual ~IUiServices() = default;

    /// 选择服务（选择状态与文档事实分离）
    virtual ISelectionService* getSelectionService() const = 0;

    /// 撤销重做管理器
    virtual IUndoRedoManager* getUndoManager() const = 0;

    /// 交互式命令生命周期分发器
    virtual IInteractionDispatcher* getInteractionDispatcher() const = 0;
};
