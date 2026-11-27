#pragma once

#include "IViewportServices.h"

class ApplicationCompositionRoot;   // AprilCore；只在 .cpp include

namespace Apr
{
    // 低耦合 adapter：把 AprilCore::ApplicationCompositionRoot 适配到 Apr::IViewportServices。
    // UI 只见 IViewportServices（has*），不见 SceneDocument2D/OperationBus/UiServices/UiWorkbench。
    class CompositionRootAdapter : public IViewportServices
    {
    public:
        explicit CompositionRootAdapter(ApplicationCompositionRoot& root);

        bool hasSceneDocument2D() const override;
        bool hasSceneDocument3D() const override;
        bool hasOperationBus() const override;
        bool hasSelectionService() const override;
        bool hasLayerManager() const override;
        bool hasUiStateCenter() const override;
        bool hasCameraController3D() const override;
        bool hasUiServices() const override;

    private:
        ApplicationCompositionRoot* m_root = nullptr;
    };
}  // namespace Apr
