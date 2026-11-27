#pragma once

#include "ICadViewport.h"

class SceneDocument2D;
class OperationBus;
class ISelectionService;
class LayerManager;

namespace Apr
{
    // 默认 2D/3D viewport factory：UI 只拿 Apr::ICadViewport。
    //   create2D -> Apr::RenderViewport2DAdapter (AprilUi::RenderViewport2D 藏后)
    //   create3D -> Apr::Viewport3DAdapter       (AprilUi::Viewport3D + RenderWidget3DAdapter 藏后)
    // 算法层注入留 adapter；UiShell 不知道 RenderViewport2D/Viewport3D。
    class DefaultViewportFactory final : public IViewportFactory
    {
    public:
        ICadViewport* create2D(QWidget* parent) override;
        ICadViewport* create3D(QWidget* parent) override;

        // 低耦合：服务来自 AprilCore；create2D 时转发给 RenderViewport2D（不暴露 raw 给 UiShell）。
        void setSceneDocument2D(::SceneDocument2D* document) { m_sceneDocument2D = document; }
        void setOperationBus(::OperationBus* bus) { m_operationBus = bus; }
        void setSelectionService(::ISelectionService* service) { m_selectionService = service; }
        void setLayerManager(::LayerManager* manager) { m_layerManager = manager; }

    private:
        ::SceneDocument2D* m_sceneDocument2D = nullptr;
        ::OperationBus* m_operationBus = nullptr;
        ::ISelectionService* m_selectionService = nullptr;
        ::LayerManager* m_layerManager = nullptr;
    };
}  // namespace Apr
