#include "AprilWorkbench.h"

#include <QString>
#include <QWidget>

namespace Apr
{
    AprilWorkbench::AprilWorkbench() = default;
    AprilWorkbench::~AprilWorkbench() = default;

    // ---------------- RD::ICenterViewProvider ----------------
    QWidget* AprilWorkbench::createCenterView(QWidget* parent)
    {
        if (m_kind == ViewportKind::ThreeD)
            return createCenterView3D(parent);
        return createCenterView2D(parent);
    }

    // ---------------- 视口形态 ----------------
    void AprilWorkbench::setViewportKind(ViewportKind kind) { m_kind = kind; }
    ViewportKind AprilWorkbench::viewportKind() const { return m_kind; }

    // ---------------- 低耦合 port ----------------
    void AprilWorkbench::setViewportFactory(IViewportFactory* factory) { m_viewportFactory = factory; }
    IViewportFactory* AprilWorkbench::viewportFactory() const { return m_viewportFactory; }
    void AprilWorkbench::setServiceFactory(ICadServiceFactory* services) { m_serviceFactory = services; }
    ICadServiceFactory* AprilWorkbench::serviceFactory() const { return m_serviceFactory; }
    IViewportServices* AprilWorkbench::viewportServices() const { return m_viewportServices; }
    ICadViewport* AprilWorkbench::cadViewport() const { return m_cadViewport; }

    void AprilWorkbench::bindViewportServices(IViewportServices& services)
    {
        // 低耦合：只保存服务视图，不持有 SceneDocument2D/OperationBus 等具体对象；
        // 服务是否齐全由 adapter（Apr::CompositionRootAdapter）回答。
        m_viewportServices = &services;
    }

    // ---------------- UiShell ShellServices 互转 ----------------
    void AprilWorkbench::populateShellServices(RD::ShellServices& shell) const
    {
        shell.centerView = const_cast<AprilWorkbench*>(this);  // RD::ICenterViewProvider*
        shell.strThemeId = m_themeId;
        shell.strStyleSeries = m_styleSeries;
        shell.strCustomStyle = m_customStyle;
        shell.cmdCatalog = m_cmdCatalog;
        shell.cmdInvoker = m_cmdInvoker;
        shell.cmdState = m_cmdState;
    }

    void AprilWorkbench::applyShellServices(const RD::ShellServices& shell)
    {
        m_themeId = shell.strThemeId;
        m_styleSeries = shell.strStyleSeries;
        m_customStyle = shell.strCustomStyle;
        m_cmdCatalog = shell.cmdCatalog;
        m_cmdInvoker = shell.cmdInvoker;
        m_cmdState = shell.cmdState;
    }

    // ---------------- 产出 ----------------
    QWidget* AprilWorkbench::centerWidget() const { return m_centerWidget; }

    // ---------------- 2D 视口：Apr::IViewportFactory + Apr::ICadViewport port ----------------
    QWidget* AprilWorkbench::createCenterView2D(QWidget* parent)
    {
        // 低耦合：UI 只拿 QWidget；旧 RenderViewport2D 藏在 adapter（Apr::RenderViewport2DAdapter）里。
        if (m_viewportFactory)
        {
            m_cadViewport = m_viewportFactory->create2D(parent);
            if (m_cadViewport && m_viewportServices)
                m_cadViewport->bindServices(*m_viewportServices);  // 经 port 注入，不暴露 raw 算法对象
            QWidget* w = m_cadViewport ? m_cadViewport->widget() : nullptr;
            if (w)
            {
                w->setObjectName(QStringLiteral("center.view"));
                m_centerWidget = w;
                return w;
            }
        }
        // 无 factory：不直接 new；空中央视图。
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        m_centerWidget = placeholder;
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
            if (m_cadViewport && m_viewportServices)
                m_cadViewport->bindServices(*m_viewportServices);
            QWidget* w = m_cadViewport ? m_cadViewport->widget() : nullptr;
            if (w)
            {
                w->setObjectName(QStringLiteral("center.view"));
                m_centerWidget = w;
                return w;
            }
        }
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        m_centerWidget = placeholder;
        return placeholder;
#else
        // 未开 BUILD_UI3D：保留调用点，不创建 3D；空中央视图。
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        m_centerWidget = placeholder;
        return placeholder;
#endif
    }
}  // namespace Apr
