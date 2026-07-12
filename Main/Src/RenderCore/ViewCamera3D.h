#pragma once

#include <QPoint>
#include <QtMath>

/**
 * @file ViewCamera3D.h
 * @brief UI 层 3D 透视相机工具类
 *
 * 封装 3D 视图的相机状态、投影计算和鼠标交互。
 * 从 RenderCoreRenderer 抽离，保持渲染器只做桥接，不做相机策略。
 *
 * 注意：与 Render/3D 的 Camera3D 区分，此版本专注于 UI 层交互和投影，
 * 使用 Qt 类型，不依赖 Engine/Render 层的 Vec3f/Mat4f。
 *
 * 职责：
 * - 管理相机姿态（yaw / pitch / distance / pan）
 * - 3D→2D 透视投影
 * - 鼠标交互（orbit / pan / zoom）
 * - 交互状态管理（旋转中、平移中、测量模式）
 *
 * 不负责：
 * - 场景编译
 * - 渲染后端选择
 * - UI 状态同步
 */
class ViewCamera3D
{
public:
    ViewCamera3D();

    // ============ 相机状态 ============

    void reset();

    void setViewportSize(int width, int height);

    bool isDirty() const
    {
        return m_dirty;
    }
    void clearDirty()
    {
        m_dirty = false;
    }

    // ============ 投影 ============

    bool project(float x, float y, float z, int& sx, int& sy) const;

    // ============ 交互 ============

    void orbit(float deltaYawDeg, float deltaPitchDeg);

    void pan(float deltaX, float deltaY);

    void zoom(float delta);

    // ============ 鼠标事件处理（统一入口） ============

    bool onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH);

    bool onMouseMove(int x, int y, int buttons, int viewW, int viewH);

    bool onMouseRelease(int x, int y, int button, int viewW, int viewH);

    bool onWheel(int delta, int viewW, int viewH);

    // ============ 模式 ============

    void setOrbitMode(bool enabled)
    {
        m_orbitMode = enabled;
    }
    bool isOrbitMode() const
    {
        return m_orbitMode;
    }

    void setMeasureMode(bool enabled)
    {
        m_measureMode = enabled;
    }
    bool isMeasureMode() const
    {
        return m_measureMode;
    }

    // ============ 交互状态查询 ============

    bool isRotating() const
    {
        return m_rotating;
    }
    bool isPanning() const
    {
        return m_panning;
    }

    // ============ 访问器 ============

    double yaw() const
    {
        return m_yaw;
    }
    double pitch() const
    {
        return m_pitch;
    }
    double distance() const
    {
        return m_distance;
    }
    double panX() const
    {
        return m_panX;
    }
    double panY() const
    {
        return m_panY;
    }

private:
    double m_yaw{ 0.0 };
    double m_pitch{ 15.0 };
    double m_distance{ 10.0 };
    double m_panX{ 0.0 };
    double m_panY{ 0.0 };

    int m_viewWidth{ 640 };
    int m_viewHeight{ 480 };

    bool m_orbitMode{ true };
    bool m_measureMode{ false };

    bool m_rotating{ false };
    bool m_panning{ false };
    QPoint m_lastMousePos;

    bool m_dirty{ false };
};