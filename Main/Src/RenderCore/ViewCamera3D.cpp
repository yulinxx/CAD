#include "ViewCamera3D.h"

ViewCamera3D::ViewCamera3D() = default;

void ViewCamera3D::reset()
{
    m_yaw = 0.0;
    m_pitch = 15.0;
    m_distance = 10.0;
    m_panX = 0.0;
    m_panY = 0.0;
    m_dirty = true;
}

void ViewCamera3D::setViewportSize(int width, int height)
{
    m_viewWidth = width;
    m_viewHeight = height;
}

bool ViewCamera3D::project(float x, float y, float z, int& sx, int& sy) const
{
    const double yawRad = qDegreesToRadians(m_yaw);
    const double pitchRad = qDegreesToRadians(m_pitch);

    const double rx = x * qCos(yawRad) + z * qSin(yawRad);
    const double ry = y;
    const double rz = -x * qSin(yawRad) + z * qCos(yawRad);

    const double ry2 = ry * qCos(pitchRad) - rz * qSin(pitchRad);
    const double rz2 = ry * qSin(pitchRad) + rz * qCos(pitchRad);

    const double fov = 60.0;
    const double scale = m_viewHeight / (2.0 * qTan(qDegreesToRadians(fov / 2.0)));
    const double camZ = rz2 + m_distance;

    if (camZ < 0.1)
        return false;

    const double projX = rx * scale / camZ;
    const double projY = ry2 * scale / camZ;

    sx = static_cast<int>(m_viewWidth / 2.0 + projX + m_panX);
    sy = static_cast<int>(m_viewHeight / 2.0 - projY + m_panY);

    return true;
}

void ViewCamera3D::orbit(float deltaYawDeg, float deltaPitchDeg)
{
    m_yaw += deltaYawDeg;
    m_pitch += deltaPitchDeg;
    m_pitch = qBound(-89.0, m_pitch, 89.0);
    m_dirty = true;
}

void ViewCamera3D::pan(float deltaX, float deltaY)
{
    m_panX += deltaX;
    m_panY += deltaY;
    m_dirty = true;
}

void ViewCamera3D::zoom(float delta)
{
    m_distance -= delta;
    m_distance = qBound(1.0, m_distance, 100.0);
    m_dirty = true;
}

bool ViewCamera3D::onMousePress(int x, int y, int button, int modifiers, int viewW, int viewH)
{
    Q_UNUSED(modifiers);
    setViewportSize(viewW, viewH);
    m_lastMousePos = QPoint(x, y);

    if (button == 2)
    {
        m_panning = true;
        return false;
    }
    else if (button == 1 && m_orbitMode)
    {
        m_rotating = true;
        return false;
    }
    return false;
}

bool ViewCamera3D::onMouseMove(int x, int y, int buttons, int viewW, int viewH)
{
    Q_UNUSED(buttons);
    setViewportSize(viewW, viewH);

    const float dx = static_cast<float>(x - m_lastMousePos.x());
    const float dy = static_cast<float>(y - m_lastMousePos.y());
    m_lastMousePos = QPoint(x, y);

    if (m_rotating)
    {
        orbit(dx * 0.5f, dy * 0.5f);
        return true;
    }
    else if (m_panning)
    {
        pan(dx, dy);
        return true;
    }
    return false;
}

bool ViewCamera3D::onMouseRelease(int x, int y, int button, int viewW, int viewH)
{
    Q_UNUSED(x); Q_UNUSED(y);
    setViewportSize(viewW, viewH);

    if (button == 2) m_panning = false;
    else if (button == 1) m_rotating = false;

    return false;
}

bool ViewCamera3D::onWheel(int delta, int viewW, int viewH)
{
    setViewportSize(viewW, viewH);
    zoom(delta * 0.01f);
    return true;
}