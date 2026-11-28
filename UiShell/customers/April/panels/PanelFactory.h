#pragma once

#include "AprilWorkbench.h"

#include "UiShell/Contract/WidgetFactory.h"   // AA::IWidgetFactory / AA::PanelCreateRequest

namespace Apr
{
    // M2/M3：April 客户的中央视图。
    // 继承 M3 的 AprilWorkbench：把现有 CAD 工作台/视口框架移植到 UiShell。
    // createCenterView 返回现有 RenderViewport2D / Viewport3D 宿主。
    class CenterView : public AprilWorkbench
    {
    };

    // 把“旧 UI 的界面部分”放进 UiShell 新框架：
    // 实现 AA::IWidgetFactory，按 panelId 产出现有旧界面部件（AprilUi / UI2D），
    // 而不是重写一个新的。factoryId = "april.panel"。
    //
    // panelId 约定（与 ui.json regions/areas 对应；旧 Workbench2D 用同名面板）：
    //   drawTools / tools -> DrawToolBarWidget     （左侧绘图工具栏，绑定 shell QAction）
    //   editToolbar       -> TopToolBar            （顶部编辑工具栏，命令顺序来自 props.commands）
    //   scene             -> SceneTreePanel        （旧场景树；service 注入后 setMode2D/3D）
    //   inspector         -> PropertiesPanelWidget （旧属性面板）
    //   status            -> StatusBar             （旧状态栏，UI2D）
    //   layers            -> SceneTreePanel / 占位 （旧图层面板占位，service 注入后接入）
    // props.kind 是显式的面板类型，优先于 panelId 的历史映射。
    // 未识别 panelId：返回空 QWidget 占位。
    class PanelFactory : public AA::IWidgetFactory
    {
    public:
        QWidget* create(const AA::PanelCreateRequest& info) override;

        // 状态栏需要视口坐标反馈；由客户模块注入 workbench，工厂只负责建控件并注册 sink。
        void setWorkbench(AprilWorkbench* workbench) { m_workbench = workbench; }

    private:
        AprilWorkbench* m_workbench = nullptr;
    };
}  // namespace Apr
