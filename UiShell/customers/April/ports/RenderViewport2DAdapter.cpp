#include "RenderViewport2DAdapter.h"

#include <QWidget>

// ---- AprilUi 旧视口：只在 adapter 内部 include ----
#include "UI/Render/RenderViewport2D.h"

namespace Apr
{
    RenderViewport2DAdapter::RenderViewport2DAdapter(QWidget* parent)
    {
        m_vp = new RenderViewport2D(parent);
    }

    RenderViewport2DAdapter::~RenderViewport2DAdapter() = default;

    QWidget* RenderViewport2DAdapter::widget() { return m_vp; }

    void RenderViewport2DAdapter::resetView() { if (m_vp) m_vp->resetView(); }
    void RenderViewport2DAdapter::zoomToSelection() { if (m_vp) m_vp->zoomToSelection(); }
    void RenderViewport2DAdapter::setDrawingEnabled(bool enabled) { if (m_vp) m_vp->setDrawingEnabled(enabled); }
    void RenderViewport2DAdapter::setMeasureMode(bool enabled) { if (m_vp) m_vp->setMeasureMode(enabled); }
    bool RenderViewport2DAdapter::setActiveTool(const QString& toolName) { return m_vp ? m_vp->setActiveTool(toolName) : false; }

    RenderViewport2D* RenderViewport2DAdapter::raw() const { return m_vp; }
}  // namespace Apr
