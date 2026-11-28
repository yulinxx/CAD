#include "Viewport3DAdapter.h"

#include <QString>
#include <QWidget>

// ---- AprilUi 3D 视口：只在 adapter 内部 include ----
#include "UI/Render/UiViewport3D.h"   // Viewport3D

#if BUILD_UI3D
#include <memory>
#include "UI/Render/RenderWidget3DAdapter.h"   // RenderWidget3DAdapter : IRenderer3D
#endif

namespace Apr
{
    Viewport3DAdapter::Viewport3DAdapter(QWidget* parent)
    {
        m_vp = new Viewport3D(parent);
#if BUILD_UI3D
        m_vp->setRenderer(std::make_unique<RenderWidget3DAdapter>());
        m_vp->initialize(nullptr);
#endif
    }

    Viewport3DAdapter::~Viewport3DAdapter() = default;

    QWidget* Viewport3DAdapter::widget() { return m_vp; }

    void Viewport3DAdapter::resetView() { if (m_vp) m_vp->resetCamera(); }
    void Viewport3DAdapter::zoomToSelection() { if (m_vp) m_vp->fitAll(); }

    // 3D 无直接“绘制/测量”；保留 port，不转发未知命令。
    void Viewport3DAdapter::setDrawingEnabled(bool /*enabled*/) {}
    void Viewport3DAdapter::setMeasureMode(bool /*enabled*/) {}
    bool Viewport3DAdapter::setActiveTool(const QString& /*toolName*/) { return false; }

    Viewport3D* Viewport3DAdapter::raw() const { return m_vp; }
}  // namespace Apr
