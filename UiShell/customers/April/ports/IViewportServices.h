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

        // UI 意图 -> 算法层 adapter 实现；不暴露具体对象。
    };
}  // namespace Apr
