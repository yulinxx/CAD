#include "DefaultViewportFactory.h"

#include "RenderViewport2DAdapter.h"
#include "Viewport3DAdapter.h"

namespace Apr
{
    ICadViewport* DefaultViewportFactory::create2D(QWidget* parent)
    {
        auto* adapter = new RenderViewport2DAdapter(parent);
        // 低耦合转发：AprilCore 服务 -> RenderViewport2D；不暴露 raw 给 UiShell。
        adapter->setSceneDocument2D(m_sceneDocument2D);
        adapter->setOperationBus(m_operationBus);
        adapter->setSelectionService(m_selectionService);
        adapter->setLayerManager(m_layerManager);
        adapter->initializeTools();
        return adapter;
    }

    ICadViewport* DefaultViewportFactory::create3D(QWidget* parent)
    {
        return new Viewport3DAdapter(parent);
    }
}  // namespace Apr
