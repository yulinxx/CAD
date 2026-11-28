#pragma once

#include <functional>
#include <memory>

#include <QString>

#include "UiShell/Contract/WidgetFactory.h"        // AA::ICenterViewProvider
#include "UiShell/Assembly/ShellServices.h"        // AA::ShellServices
#include "ports/ICadViewport.h"                    // Apr::ICadViewport + Apr::IViewportFactory
#include "ports/IViewportServices.h"               // Apr::IViewportServices
#include "ports/ICadServiceFactory.h"             // Apr::ICadServiceFactory

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
    // 由 customers/April 持有，经注入提供；并可与 UiShell 的 AA::ShellServices 互转。
    //
    // 说明：本类“移植框架”，本轮不要求编译；CAD 组合根（ApplicationCompositionRoot，
    // 位于 Main/Src/Composition，不在 AprilUi 内）如何为 customers/April 提供
    // 已装配好的 UiServices / SceneDocument2D，在后续处理 Engine 子模块时一并接上。
    enum class ViewportKind
    {
        TwoD,    ///< RenderViewport2D
        ThreeD   ///< Viewport3D + RenderWidget3DAdapter（需 BUILD_UI3D）
    };

    class AprilWorkbench : public AA::ICenterViewProvider
    {
    public:
        AprilWorkbench();
        ~AprilWorkbench() override;

        // ---------------- AA::ICenterViewProvider ----------------
        QWidget* createCenterView(QWidget* parent) override;

        // ---------------- 视口形态 ----------------
        void setViewportKind(ViewportKind kind);
        ViewportKind viewportKind() const;

        // ---------------- CAD 视口服务（由 customers/April 持有并注入）----------------








        // ---------------- 低耦合 port ----------------
        // UI 只走 Apr::IViewportFactory + Apr::ICadViewport；不持有/不暴露 RenderViewport2D/Viewport3D/SceneDocument2D/OperationBus 给 UiShell。
        void setViewportFactory(IViewportFactory* factory);

        // 坐标反馈下沉点：客户面板（状态栏）注册 sink，workbench 把它装到当前/后续视口。
        // 外壳与旧视口都不需要知道状态栏的存在。
        using PositionSink = std::function<void(double, double)>;
        void setPositionSink(PositionSink sink);

        // 低耦合：service factory + services view，不直接 set SceneDocument2D/OperationBus/...
        void setServiceFactory(ICadServiceFactory* services);
        ICadServiceFactory* serviceFactory() const;
        IViewportServices* viewportServices() const;  ///< 已注入的服务视图（仅 customer 层可见）
        IViewportFactory* viewportFactory() const;
        ICadViewport* cadViewport() const;   ///< 最近 createCenterView 产出的 port（不暴露 raw 旧视口）

        // ---------------- 组合根注入（M3.5：ApplicationCompositionRoot，在 AprilCore）----------------
        // 若 root.isBuilt()，把已装配的 UiServices / SceneDocument2D / UiStateCenter /
        // OperationBus / ISelectionService / LayerManager 注入本视口。
        // 返回是否实际注入。root 未 build() 时什么都不做。
        // 低耦合：UI 只见 Apr::IViewportServices，不见 ApplicationCompositionRoot/SceneDocument2D/OperationBus。
        void bindViewportServices(IViewportServices& services);

        // ---------------- UiShell ShellServices 互转 ----------------
        // 把本客户持有的信息写进 UiShell 的 ShellServices（centerView/cmd*/style）。
        void populateShellServices(AA::ShellServices& shell) const;
        // 读取 UiShell 给过来的 ShellServices（主题/样式/命令等）。
        void applyShellServices(const AA::ShellServices& shell);

        // ---------------- 产出 ----------------
        QWidget* centerWidget() const;             ///< 最近一次 createCenterView 返回的部件

    protected:
        QWidget* createCenterView2D(QWidget* parent);
        QWidget* createCenterView3D(QWidget* parent);

    private:
        void attachViewportCallbacks();

        ViewportKind m_kind = ViewportKind::TwoD;



        QWidget* m_centerWidget = nullptr;
        IViewportFactory* m_viewportFactory = nullptr;
        PositionSink m_positionSink;
        ICadServiceFactory* m_serviceFactory = nullptr;
        IViewportServices* m_viewportServices = nullptr;
        ICadViewport* m_cadViewport = nullptr;      // 不持有；factory 产出，widget() 交给 Qt

        // UiShell 侧只读信息
        QString m_themeId;
        QString m_styleSeries;
        QString m_customStyle;
        const AA::ICommandCatalog* m_cmdCatalog = nullptr;
        AA::ICommandInvoker* m_cmdInvoker = nullptr;
        const AA::ICommandState* m_cmdState = nullptr;
    };
}  // namespace Apr
