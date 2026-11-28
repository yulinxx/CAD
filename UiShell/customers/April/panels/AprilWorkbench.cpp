#include "AprilWorkbench.h"

#include <utility>

#include <QString>
#include <QWidget>

namespace Apr
{
    AprilWorkbench::AprilWorkbench() = default;
    AprilWorkbench::~AprilWorkbench() = default;

    // ---------------- AA::ICenterViewProvider ----------------
    QWidget* AprilWorkbench::createCenterView(QWidget* parent)
    {
        if (m_kind == ViewportKind::ThreeD)
            return createCenterView3D(parent);
        return createCenterView2D(parent);
    }

    // ---------------- 视口形态 ----------------
    void AprilWorkbench::setViewportKind(ViewportKind kind) { m_kind = kind; }

    // ---------------- 低耦合 port ----------------
    void AprilWorkbench::setViewportFactory(IViewportFactory* factory) { m_viewportFactory = factory; }

    void AprilWorkbench::setPositionSink(PositionSink sink)
    {
        m_positionSink = std::move(sink);
        attachViewportCallbacks();
    }

    void AprilWorkbench::attachViewportCallbacks()
    {
        if (m_cadViewport)
            m_cadViewport->setPositionCallback(m_positionSink);
    }
    ICadViewport* AprilWorkbench::cadViewport() const { return m_cadViewport; }

    // ---------------- 2D 视口：Apr::IViewportFactory + Apr::ICadViewport port ----------------
    QWidget* AprilWorkbench::createCenterView2D(QWidget* parent)
    {
        // 低耦合：UI 只拿 QWidget；旧 RenderViewport2D 藏在 adapter（Apr::RenderViewport2DAdapter）里。
        if (m_viewportFactory)
        {
            m_cadViewport = m_viewportFactory->create2D(parent);
            attachViewportCallbacks();
            QWidget* w = m_cadViewport ? m_cadViewport->widget() : nullptr;
            if (w)
            {
                w->setObjectName(QStringLiteral("center.view"));
                return w;
            }
        }
        // 无 factory：不直接 new；空中央视图。
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        return placeholder;
    }

    // ---------------- 3D 视口：Apr::IViewportFactory + Apr::ICadViewport port ----------------
    QWidget* AprilWorkbench::createCenterView3D(QWidget* parent)
    {
#if BUILD_UI3D
        // 低耦合：旧 Viewport3D + RenderWidget3DAdapter 藏在 adapter（Apr::Viewport3DAdapter）里。
        if (m_viewportFactory)
        {
            m_cadViewport = m_viewportFactory->create3D(parent);
            attachViewportCallbacks();
            QWidget* w = m_cadViewport ? m_cadViewport->widget() : nullptr;
            if (w)
            {
                w->setObjectName(QStringLiteral("center.view"));
                return w;
            }
        }
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        return placeholder;
#else
        // 未开 BUILD_UI3D：保留调用点，不创建 3D；空中央视图。
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        return placeholder;
#endif
    }
}  // namespace Apr
