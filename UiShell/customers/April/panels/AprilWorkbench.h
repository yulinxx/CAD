#pragma once

#include <functional>

#include "UiShell/Contract/WidgetFactory.h"        // AA::ICenterViewProvider
#include "ports/ICadViewport.h"                    // Apr::ICadViewport + Apr::IViewportFactory

namespace Apr
{
    // 客户层中央视图：实现 AA::ICenterViewProvider，复用现有 RenderViewport2D / Viewport3D。
    // 只经 ports（IViewportFactory / ICadViewport）与算法层通信，
    // 不持有 Qt 部件之外的 UI 服务或引擎对象。
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

        // ---------------- 低耦合 port ----------------
        // UI 只走 Apr::IViewportFactory + Apr::ICadViewport；不持有/不暴露 RenderViewport2D/Viewport3D/SceneDocument2D/OperationBus 给 UiShell。
        void setViewportFactory(IViewportFactory* factory);

        // 坐标反馈下沉点：客户面板（状态栏）注册 sink，workbench 把它装到当前/后续视口。
        // 外壳与旧视口都不需要知道状态栏的存在。
        using PositionSink = std::function<void(double, double)>;
        void setPositionSink(PositionSink sink);

        ICadViewport* cadViewport() const;   ///< 最近 createCenterView 产出的 port（不暴露 raw 旧视口）

    protected:
        QWidget* createCenterView2D(QWidget* parent);
        QWidget* createCenterView3D(QWidget* parent);

    private:
        void attachViewportCallbacks();

        ViewportKind m_kind = ViewportKind::TwoD;
        IViewportFactory* m_viewportFactory = nullptr;
        PositionSink m_positionSink;
        ICadViewport* m_cadViewport = nullptr;      // 不持有；factory 产出，widget() 交给 Qt
    };
}  // namespace Apr
