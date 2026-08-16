#pragma once

#include <QPoint>
#include <QPointF>
#include <QSizeF>
#include <functional>

class QWheelEvent;
class QNativeGestureEvent;
class RenderWidget;
class Camera2D;

/**
 * @brief 2D 视口导航控制器 — 相机 + 手势 + 坐标换算的单一实现
 *
 * 主视口（ViewportInputRouter）与所有独立预览窗口（TestView 等）共用同一份
 * “手势 → 相机变化” 逻辑，避免多窗口各自维护一套滚轮/触控板/平移/坐标换算代码。
 *
 * 职责：
 *   - 对注入的 Camera2D 施加缩放/平移/适配
 *   - 滚轮/触控板手势分类与处理（缩放、双指平移、水平平移、捏合缩放）
 *   - 鼠标拖拽平移
 *   - 物理像素 ⇄ 世界坐标换算（含 HiDPI/DPR）
 *   - 相机变化回调（通知宿主提交视图矩阵并重绘）
 *
 * 依赖：注入 RenderWidget（获取视口尺寸与 DPR）和 Camera2D（由宿主拥有）。
 * 本类不拥有 Camera2D，只操作它，便于主视口与预览窗口共享同一实现。
 */
class ViewportNavigation2D
{
public:
    /// 滚轮/触控板手势分类结果
    enum class WheelGestureType
    {
        Zoom,           ///< 缩放
        Pan,            ///< 平移（双指拖动）
        HorizontalPan,  ///< 水平平移（Shift+双指）
    };

    // ---- 依赖注入 ----

    void setRenderWidget(const RenderWidget* widget);
    /// 相机由宿主拥有，本类仅持有非拥有指针并操作它
    void setCamera(Camera2D* camera);
    Camera2D* camera() const;
    /// 相机变化回调 — 缩放/平移后通知宿主更新视图矩阵并重绘
    void setCameraChangedCallback(std::function<void()> callback);

    // ---- 事件处理（纯导航，不涉及选择/编辑） ----

    void handleWheel(QWheelEvent* event);
    void handleNativeGesture(QNativeGestureEvent* event);

    void beginPan(const QPoint& physWidgetPos);
    void updatePan(const QPoint& physWidgetPos);
    void endPan();
    bool isPanning() const
    {
        return m_panning;
    }

    // ---- 坐标换算（物理像素 ⇄ 世界） ----

    QPointF widgetToWorld(const QPointF& widgetLocalPos) const;
    QPointF physicalToWorld(const QPoint& physPos) const;
    QSizeF physicalViewportSize() const;
    float physicalWidth() const;
    float physicalHeight() const;

    /// 跨平台滚轮/触控板手势分类（纯函数，便于单测）：
    ///  - 无滚动阶段(普通鼠标滚轮) = 缩放
    ///  - 触控板(带滚动阶段)：Ctrl = 捏合缩放，Shift = 水平平移，其余 = 平移
    static WheelGestureType classifyWheel(
        const QPoint& angleDelta, const QPointF& pixelDelta, Qt::KeyboardModifiers modifiers, Qt::ScrollPhase phase);

private:
    void notifyCameraChanged();

    const RenderWidget* m_renderWidget{ nullptr };
    Camera2D* m_camera{ nullptr };
    bool m_panning{ false };
    QPoint m_lastMousePos;
    std::function<void()> m_cameraChangedCallback;
};
