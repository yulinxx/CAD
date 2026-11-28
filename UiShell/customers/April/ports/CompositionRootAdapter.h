#pragma once

#include "IViewportServices.h"
#include "ICadServiceFactory.h"
#include "DefaultViewportFactory.h"

class ApplicationCompositionRoot;   // AprilCore；只在 .cpp include

namespace Apr
{
    class OperationCommandSink;

    // 低耦合 adapter：把 AprilCore::ApplicationCompositionRoot 适配到 Apr::IViewportServices。
    // UI 只见 IViewportServices（has*），不见 SceneDocument2D/OperationBus/UiServices/UiWorkbench。
    class CompositionRootAdapter : public IViewportServices, public ICadServiceFactory
    {
    public:
        explicit CompositionRootAdapter(ApplicationCompositionRoot& root);

        /// 注入客户侧命令出口，供 dispatchOperation() 复用（M6）。
        void setCommandSink(OperationCommandSink* sink) { m_commandSink = sink; }

        bool hasSceneDocument2D() const override;
        bool hasSceneDocument3D() const override;
        bool hasOperationBus() const override;
        bool hasSelectionService() const override;
        bool hasLayerManager() const override;
        bool hasUiStateCenter() const override;
        bool hasCameraController3D() const override;
        bool hasUiServices() const override;

        // 抽象访问 + 转发（不暴露 raw 给 UiShell；算法在 AprilCore）
        bool canUndo() const override;
        bool canRedo() const override;
        void undo() override;
        void redo() override;
        bool dispatchOperation(const QString& commandId) override;

        // ICadServiceFactory
        IViewportFactory* viewportFactory() override;
        IViewportServices* viewportServices() override;

    private:
        ApplicationCompositionRoot* m_root = nullptr;
        OperationCommandSink* m_commandSink = nullptr;
        DefaultViewportFactory m_viewportFactory;
    };
}  // namespace Apr
