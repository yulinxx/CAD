#pragma once

#include <QString>

namespace Apr
{
    // 低耦合 port：UI 不持有 SceneDocument2D/SceneDocument3D/OperationBus/ISelectionService/LayerManager/CameraController3D/UiStateCenter/UiServices。
    // 由 Apr::CompositionRootAdapter 从 AprilCore::ApplicationCompositionRoot 适配；UiShell 不见 Main。
    class IViewportServices
    {
    public:
        virtual ~IViewportServices() = default;

        virtual bool hasSceneDocument2D() const = 0;
        virtual bool hasSceneDocument3D() const = 0;
        virtual bool hasOperationBus() const = 0;
        virtual bool hasSelectionService() const = 0;
        virtual bool hasLayerManager() const = 0;
        virtual bool hasUiStateCenter() const = 0;
        virtual bool hasCameraController3D() const = 0;
        virtual bool hasUiServices() const = 0;

        // 低耦合：不向 UI 暴露具体对象；UI 意图 -> 算法层 adapter。
        // 这里保持抽象；具体 adapter（Apr::CompositionRootAdapter）负责转发到 AprilCore。

        // ---- 抽象访问 + 转发（M3 运行时公共前提）----
        // 算法（SceneDocument2D/OperationBus/...）在 AprilCore；这里不返回 raw 给 UiShell。
        // 2D 场景文档查询转发
        virtual bool sceneDocument2DValid() const { return hasSceneDocument2D(); }
        virtual bool sceneDocument3DValid() const { return hasSceneDocument3D(); }

        // 操作总线 / 撤销重做（转发到 AprilCore::OperationBus / IUndoRedoManager；不暴露 raw）
        virtual bool canUndo() const = 0;
        virtual bool canRedo() const = 0;
        virtual void undo() = 0;
        virtual void redo() = 0;

        // UI 意图 -> 算法层
        virtual bool dispatchOperation(const QString& commandId) = 0;   // OperationBus::run / IUiCommandDispatcher::dispatch
    };
}  // namespace Apr
