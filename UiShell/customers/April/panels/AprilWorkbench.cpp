#include "AprilWorkbench.h"

#include <QString>
#include <QWidget>

// ---- AprilUi 视口 ----
#include "UI/Render/RenderViewport2D.h"   // RenderViewport2D
#include "UI/Render/UiViewport3D.h"       // Viewport3D

// ---- 组合根（AprilCore）----
#include "Composition/ApplicationCompositionRoot.h"

#if BUILD_UI3D
#include "UI/Render/RenderWidget3DAdapter.h"  // RenderWidget3DAdapter : IRenderer3D
#endif

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

    // ---------------- CAD 视口服务 ----------------
    void AprilWorkbench::setUiServices(const UiServices& services)
    {
        m_uiServices = services;                 // 七分组装配容器整体复制
        m_workbenchServices = services.workbench();  // 工作台分组同步
    }
    const UiServices& AprilWorkbench::uiServices() const { return m_uiServices; }

    void AprilWorkbench::setWorkbenchServices(const WorkbenchServices& services)
    {
        m_workbenchServices = services;
    }
    const WorkbenchServices& AprilWorkbench::workbenchServices() const { return m_workbenchServices; }

    void AprilWorkbench::setSceneDocument2D(SceneDocument2D* document) { m_sceneDocument2D = document; }
    SceneDocument2D* AprilWorkbench::sceneDocument2D() const { return m_sceneDocument2D; }

    void AprilWorkbench::setSceneDocument3D(SceneDocument3D* document) { m_sceneDocument3D = document; }
    SceneDocument3D* AprilWorkbench::sceneDocument3D() const { return m_sceneDocument3D; }

    void AprilWorkbench::setUiStateCenter(UiStateCenter* center) { m_uiStateCenter = center; }
    UiStateCenter* AprilWorkbench::uiStateCenter() const { return m_uiStateCenter; }

    void AprilWorkbench::setOperationBus(OperationBus* bus) { m_operationBus = bus; }
    OperationBus* AprilWorkbench::operationBus() const { return m_operationBus; }

    void AprilWorkbench::setSelectionService(ISelectionService* service) { m_selectionService = service; }
    ISelectionService* AprilWorkbench::selectionService() const { return m_selectionService; }

    void AprilWorkbench::setLayerManager(LayerManager* manager) { m_layerManager = manager; }
    LayerManager* AprilWorkbench::layerManager() const { return m_layerManager; }

    // ---------------- 低耦合 port ----------------
    void AprilWorkbench::setViewportFactory(IViewportFactory* factory) { m_viewportFactory = factory; }
    IViewportFactory* AprilWorkbench::viewportFactory() const { return m_viewportFactory; }
    ICadViewport* AprilWorkbench::cadViewport() const { return m_cadViewport; }

    void AprilWorkbench::setCameraController3D(CameraController3D* controller) { m_cameraController3D = controller; }
    CameraController3D* AprilWorkbench::cameraController3D() const { return m_cameraController3D; }

    void AprilWorkbench::bindViewportServices(IViewportServices& services)
    {
        // 不暴露具体对象；服务是否齐全由 adapter（Apr::CompositionRootAdapter）回答。
        Q_UNUSED(services);
    }

    // ---------------- 组合根注入（M3.5）----------------
    bool AprilWorkbench::bindCompositionRoot(ApplicationCompositionRoot& root)
    {
        if (!root.isBuilt())
            return false;

        setUiServices(root.uiServices());                 // 七分组 + workbench()
        setWorkbenchServices(root.uiServices().workbench());
        if (root.document2D())
            setSceneDocument2D(root.document2D());
        if (root.stateCenter())
            setUiStateCenter(root.stateCenter());
        if (root.operationBus())
            setOperationBus(root.operationBus());
        if (root.selectionService())
            setSelectionService(root.selectionService());
        if (root.layerManager())
            setLayerManager(root.layerManager());
        return true;
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
    RenderViewport2D* AprilWorkbench::viewport2D() const { return m_viewport2D; }
    Viewport3D* AprilWorkbench::viewport3D() const { return m_viewport3D; }

    // ---------------- 2D 视口：优先走 Apr::IViewportFactory + Apr::ICadViewport port ----------------
    QWidget* AprilWorkbench::createCenterView2D(QWidget* parent)
    {
        // 低耦合：UI 只拿 QWidget；旧 RenderViewport2D 藏在 adapter（Apr::RenderViewport2DAdapter）里。
        if (m_viewportFactory)
        {
            m_cadViewport = m_viewportFactory->create2D(parent);
            QWidget* w = m_cadViewport ? m_cadViewport->widget() : nullptr;
            if (w)
            {
                w->setObjectName(QStringLiteral("center.view"));
                m_viewport2D = nullptr;   // 不暴露 raw 旧视口给 UiShell
                m_viewport3D = nullptr;
                m_centerWidget = w;
                return w;
            }
        }

        // 无 factory：不再直接 new RenderViewport2D；返回空中央视图（factory 由 customer/adapter 提供）。
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        m_centerWidget = placeholder;
        return placeholder;
    }

    // ---------------- 3D 视口：复用 AprilUi 的 Viewport3D + RenderWidget3DAdapter ----------------
    QWidget* AprilWorkbench::createCenterView3D(QWidget* parent)
    {
#if BUILD_UI3D
        // 低耦合：UI 只拿 QWidget；旧 Viewport3D + RenderWidget3DAdapter 藏在 adapter（Apr::Viewport3DAdapter）里。
        if (m_viewportFactory)
        {
            m_cadViewport = m_viewportFactory->create3D(parent);
            QWidget* w = m_cadViewport ? m_cadViewport->widget() : nullptr;
            if (w)
            {
                w->setObjectName(QStringLiteral("center.view"));
                m_viewport3D = nullptr;   // 不暴露 raw 旧视口给 UiShell
                m_viewport2D = nullptr;
                m_centerWidget = w;
                return w;
            }
        }
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        m_centerWidget = placeholder;
        return placeholder;
#else
        // 未开 BUILD_UI3D：3D 框架保留调用点，但不创建 3D 视口；退回空中央视图。
        QWidget* placeholder = new QWidget(parent);
        placeholder->setObjectName(QStringLiteral("center.view"));
        m_viewport3D = nullptr;
        m_viewport2D = nullptr;
        m_centerWidget = placeholder;
        return placeholder;
#endif
    }
}  // namespace Apr
