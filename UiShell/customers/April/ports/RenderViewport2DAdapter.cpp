#include "RenderViewport2DAdapter.h"

#include <utility>

#include <QWidget>

// ---- AprilUi 旧视口：只在 adapter 内部 include ----
#include "UI/Render/RenderViewport2D.h"
#include "UI/Documents/SceneDocument2D.h"
#include "UI2D/Operation/OperationBus.h"
#include "UI/Services/SelectionService.h"
#include "Engine2D/Interaction/LayerManager.h"

namespace Apr
{
    RenderViewport2DAdapter::RenderViewport2DAdapter(QWidget* parent)
    {
        m_vp = new RenderViewport2D(parent);
    }

    RenderViewport2DAdapter::~RenderViewport2DAdapter() = default;

    QWidget* RenderViewport2DAdapter::widget() { return m_vp; }

    void RenderViewport2DAdapter::bindServices(IViewportServices& services) { m_services = &services; }

    void RenderViewport2DAdapter::setPositionCallback(PositionCallback callback)
    {
        if (m_vp)
            m_vp->setPositionCallback(std::move(callback));
    }

    void RenderViewport2DAdapter::resetView() { if (m_vp) m_vp->resetView(); }
    void RenderViewport2DAdapter::zoomToSelection() { if (m_vp) m_vp->zoomToSelection(); }
    void RenderViewport2DAdapter::setDrawingEnabled(bool enabled) { if (m_vp) m_vp->setDrawingEnabled(enabled); }
    void RenderViewport2DAdapter::setMeasureMode(bool enabled) { if (m_vp) m_vp->setMeasureMode(enabled); }
    bool RenderViewport2DAdapter::setActiveTool(const QString& toolName) { return m_vp ? m_vp->setActiveTool(toolName) : false; }

    void RenderViewport2DAdapter::setSceneDocument2D(::SceneDocument2D* document) { if (m_vp) m_vp->setDocument(document); }
    void RenderViewport2DAdapter::setOperationBus(::OperationBus* bus) { if (m_vp) m_vp->setOperationBus(bus); }
    void RenderViewport2DAdapter::setSelectionService(::ISelectionService* service) { if (m_vp) m_vp->setSelectionService(service); }
    void RenderViewport2DAdapter::setLayerManager(::LayerManager* manager) { if (m_vp) m_vp->setLayerManager(manager); }
    void RenderViewport2DAdapter::initializeTools() { if (m_vp) m_vp->initializeTools(); }

    RenderViewport2D* RenderViewport2DAdapter::raw() const { return m_vp; }
}  // namespace Apr
