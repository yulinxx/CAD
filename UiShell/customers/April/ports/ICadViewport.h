#pragma once

#include <functional>

#include <QString>
#include <QWidget>

namespace Apr
{
    // 低耦合 port：UI 不直接持有 SceneDocument2D/OperationBus/SceneManager。
    // customers/April 提供 adapter（RenderViewport2DAdapter / Viewport3DAdapter）把旧 CAD 视口藏起来；
    // UI 只发“意图”，算法/场景编辑仍在 AprilCore::OperationBus/SceneDocument2D。
    class ICadViewport
    {
    public:
        virtual ~ICadViewport() = default;

        virtual QWidget* widget() = 0;                 // 交给 UiShell::ICenterViewProvider

        using PositionCallback = std::function<void(double, double)>;

        // 鼠标世界坐标回调：默认空实现，只有支持坐标反馈的视口才 override。
        virtual void setPositionCallback(PositionCallback) {}

        // UI 意图 -> 算法层（不暴露 SceneManager/SceneDocument2D）
        virtual void resetView() = 0;
        virtual void zoomToSelection() = 0;
        virtual void setDrawingEnabled(bool enabled) = 0;
        virtual void setMeasureMode(bool enabled) = 0;
        virtual bool setActiveTool(const QString& toolName) = 0;
    };

    // 视口工厂：UI 不知道 RenderViewport2D/Viewport3D。
    class IViewportFactory
    {
    public:
        virtual ~IViewportFactory() = default;
        virtual ICadViewport* create2D(QWidget* parent) = 0;   // RenderViewport2DAdapter
        virtual ICadViewport* create3D(QWidget* parent) = 0;   // Viewport3DAdapter
    };
}  // namespace Apr
