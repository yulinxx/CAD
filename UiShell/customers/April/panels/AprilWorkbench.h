#pragma once

#include <memory>

#include <QString>

#include "UiShell/Contract/WidgetFactory.h"        // RD::ICenterViewProvider
#include "UiShell/Assembly/ShellServices.h"        // RD::ShellServices
#include "ports/ICadViewport.h"                    // Apr::ICadViewport + Apr::IViewportFactory

// ---- AprilUi（Main/Src/UI，静态库 AprilUi，M1）----
#include "UI/Services/UiServices.h"                // UiServices（七分组装配容器）
#include "UI/Services/UiServiceGroups.h"           // WorkbenchServices
#include "UI/Services/UiStateCenter.h"             // UiStateCenter

// 前向声明，避免把 CAD 引擎头拖进客户公共接口。
class SceneDocument2D;
class SceneDocument3D;
class OperationBus;
class ISelectionService;
class LayerManager;
class CameraController3D;
class ApplicationCompositionRoot;   // Main/Src/Composition（AprilCore，M3.5）
class RenderViewport2D;  // Main/Src/UI/Render/RenderViewport2D.h
class Viewport3D;        // Main/Src/UI/Render/UiViewport3D.h（类名是 Viewport3D，不是 UiViewport3D）

namespace Apr
{
    // M3：把现有 CAD 工作台/视口框架移植到 UiShell 的 ICenterViewProvider。
    //
    // 不再重写工作台；直接复用 AprilUi 里的 RenderViewport2D / Viewport3D。
    // 视口所需服务（UiServices / SceneDocument2D / UiStateCenter / OperationBus /
    // ISelectionService / LayerManager / SceneDocument3D / CameraController3D）
    // 由 customers/April 持有，经注入提供；并可与 UiShell 的 RD::ShellServices 互转。
    //
    // 说明：本类“移植框架”，本轮不要求编译；CAD 组合根（ApplicationCompositionRoot，
    // 位于 Main/Src/Composition，不在 AprilUi 内）如何为 customers/April 提供
    // 已装配好的 UiServices / SceneDocument2D，在后续处理 Engine 子模块时一并接上。
    enum class ViewportKind
    {
        TwoD,    ///< RenderViewport2D
        ThreeD   ///< Viewport3D + RenderWidget3DAdapter（需 BUILD_UI3D）
    };

    class AprilWorkbench : public RD::ICenterViewProvider
    {
    public:
        AprilWorkbench();
        ~AprilWorkbench() override;

        // ---------------- RD::ICenterViewProvider ----------------
        QWidget* createCenterView(QWidget* parent) override;

        // ---------------- 视口形态 ----------------
        void setViewportKind(ViewportKind kind);
        ViewportKind viewportKind() const;

        // ---------------- CAD 视口服务（由 customers/April 持有并注入）----------------
        void setUiServices(const UiServices& services);     ///< 复制一份装配容器（七分组）
        const UiServices& uiServices() const;
        void setWorkbenchServices(const WorkbenchServices& services);
        const WorkbenchServices& workbenchServices() const;

        void setSceneDocument2D(SceneDocument2D* document); ///< 2D 场景文档
        SceneDocument2D* sceneDocument2D() const;

        void setSceneDocument3D(SceneDocument3D* document); ///< 3D 场景文档（BUILD_UI3D）
        SceneDocument3D* sceneDocument3D() const;

        void setUiStateCenter(UiStateCenter* center);       ///< UI 状态中心
        UiStateCenter* uiStateCenter() const;

        void setOperationBus(OperationBus* bus);            ///< 操作总线（撤销/重做等）
        OperationBus* operationBus() const;

        void setSelectionService(ISelectionService* service);
        ISelectionService* selectionService() const;

        void setLayerManager(LayerManager* manager);        ///< 图层管理器
        LayerManager* layerManager() const;

        void setCameraController3D(CameraController3D* controller);
        CameraController3D* cameraController3D() const;

        // ---------------- 低耦合 port ----------------
        // UI 只走 Apr::IViewportFactory + Apr::ICadViewport；不持有/不暴露 RenderViewport2D/Viewport3D/SceneDocument2D/OperationBus 给 UiShell。
        void setViewportFactory(IViewportFactory* factory);
        IViewportFactory* viewportFactory() const;
        ICadViewport* cadViewport() const;   ///< 最近 createCenterView 产出的 port（不暴露 raw 旧视口）

        // ---------------- 组合根注入（M3.5：ApplicationCompositionRoot，在 AprilCore）----------------
        // 若 root.isBuilt()，把已装配的 UiServices / SceneDocument2D / UiStateCenter /
        // OperationBus / ISelectionService / LayerManager 注入本视口。
        // 返回是否实际注入。root 未 build() 时什么都不做。
        bool bindCompositionRoot(ApplicationCompositionRoot& root);

        // ---------------- UiShell ShellServices 互转 ----------------
        // 把本客户持有的信息写进 UiShell 的 ShellServices（centerView/cmd*/style）。
        void populateShellServices(RD::ShellServices& shell) const;
        // 读取 UiShell 给过来的 ShellServices（主题/样式/命令等）。
        void applyShellServices(const RD::ShellServices& shell);

        // ---------------- 产出 ----------------
        QWidget* centerWidget() const;             ///< 最近一次 createCenterView 返回的部件
        RenderViewport2D* viewport2D() const;
        Viewport3D* viewport3D() const;

    protected:
        QWidget* createCenterView2D(QWidget* parent);
        QWidget* createCenterView3D(QWidget* parent);

    private:
        ViewportKind m_kind = ViewportKind::TwoD;

        UiServices m_uiServices;                   // 装配容器（由 ApplicationCompositionRoot 填充，M3 接上）
        WorkbenchServices m_workbenchServices;     // 工作台分组（m_uiServices.workbench() 或单独注入）

        SceneDocument2D* m_sceneDocument2D = nullptr;
        SceneDocument3D* m_sceneDocument3D = nullptr;
        UiStateCenter* m_uiStateCenter = nullptr;
        OperationBus* m_operationBus = nullptr;
        ISelectionService* m_selectionService = nullptr;
        LayerManager* m_layerManager = nullptr;
        CameraController3D* m_cameraController3D = nullptr;

        QWidget* m_centerWidget = nullptr;
        RenderViewport2D* m_viewport2D = nullptr;   // 不持有，Qt parent 树负责生命周期（不暴露给 UiShell；用 port）
        Viewport3D* m_viewport3D = nullptr;         // 不持有
        IViewportFactory* m_viewportFactory = nullptr;
        ICadViewport* m_cadViewport = nullptr;      // 不持有；factory 产出，widget() 交给 Qt

        // UiShell 侧只读信息
        QString m_themeId;
        QString m_styleSeries;
        QString m_customStyle;
        const RD::ICommandCatalog* m_cmdCatalog = nullptr;
        RD::ICommandInvoker* m_cmdInvoker = nullptr;
        const RD::ICommandState* m_cmdState = nullptr;
    };
}  // namespace Apr
