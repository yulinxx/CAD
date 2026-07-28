#include "Camera2D.h"

#include <QPoint>
#include <cmath>

#include "Log/SyLogger.h"
#include "Render/RenderTypes.h"

namespace
{
    void mat3ToArray(const Render::Mat3f& mat, float out[9])
    {
        for (int i = 0; i < 9; ++i)
            out[i] = mat.data[i];
    }
}

void Camera2D::computeViewMatrix(float outMat[9], float vpW, float vpH) const
{
    if (vpW <= 0 || vpH <= 0)
    {
        for (int i = 0; i < 9; ++i)
            outMat[i] = (i == 0 || i == 4 || i == 8) ? 1.0f : 0.0f;
        SY_WARNF("Camera2D::computeViewMatrix: invalid viewport %.2fx%.2f, returning identity", vpW, vpH);
        return;
    }

    const float scaleX = 2.0f * zoomX / vpW;
    const float scaleY = 2.0f * zoomY / vpH;
    const float tx = scaleX * panOffset.x();
    const float ty = scaleY * panOffset.y();

    Render::Mat3f view = Render::Mat3f::identity();
    view.at(0, 0) = scaleX;
    view.at(1, 0) = 0.0f;
    view.at(2, 0) = 0.0f;
    view.at(0, 1) = 0.0f;
    view.at(1, 1) = scaleY;
    view.at(2, 1) = 0.0f;
    view.at(0, 2) = tx;
    view.at(1, 2) = ty;
    view.at(2, 2) = 1.0f;

    // SY_INFOF("Camera2D::computeViewMatrix: vp=(%.0f,%.0f), zoom=(%.6f,%.6f), pan=(%.2f,%.2f), mat=[%.4f,%.4f,%.4f, %.4f,%.4f,%.4f]",
    //     vpW, vpH, zoomX, zoomY, panOffset.x(), panOffset.y(),
    //     view.at(0,0), view.at(0,2), view.at(1,1), view.at(1,2));

    mat3ToArray(view, outMat);
}

QPointF Camera2D::screenToWorld(const QPoint& screenPos, float vpW, float vpH) const
{
    if (vpW <= 0 || vpH <= 0)
        return QPointF(0, 0);

    // 屏幕坐标转换为标准 OpenGL NDC（x: -1左~1右, y: -1下~1上）
    float nx = (2.0f * screenPos.x() - vpW) / vpW;
    float ny = (vpH - 2.0f * screenPos.y()) / vpH;

    if (zoomX < 1e-6f || zoomY < 1e-6f)
        return QPointF(nx, ny);

    float scaleX = 2.0f * zoomX / vpW;
    float scaleY = 2.0f * zoomY / vpH;

    float wx = (nx / scaleX) - panOffset.x();
    float wy = (ny / scaleY) - panOffset.y();

    // SY_INFOF("Camera2D::screenToWorld: screen=(%d,%d), vp=(%.0f,%.0f), ndc=(%.3f,%.3f), zoom=(%.6f,%.6f), pan=(%.2f,%.2f), world=(%.2f,%.2f)",
    //     screenPos.x(), screenPos.y(), vpW, vpH, nx, ny, zoomX, zoomY, panOffset.x(), panOffset.y(), wx, wy);

    return QPointF(wx, wy);
}

void Camera2D::zoomIn(float factor, const QPointF& anchorWorld, float vpW, float vpH)
{
    float newZoomX = zoomX * factor;
    float newZoomY = zoomY * factor;
    if (newZoomX > MAX_ZOOM) newZoomX = MAX_ZOOM;
    if (newZoomX < MIN_ZOOM) newZoomX = MIN_ZOOM;
    if (newZoomY > MAX_ZOOM) newZoomY = MAX_ZOOM;
    if (newZoomY < MIN_ZOOM) newZoomY = MIN_ZOOM;

    float ratio = newZoomX / zoomX;
    panOffset.setX(panOffset.x() / ratio + anchorWorld.x() * (1.0f / ratio - 1.0f));
    panOffset.setY(panOffset.y() / ratio + anchorWorld.y() * (1.0f / ratio - 1.0f));

    zoomX = newZoomX;
    zoomY = newZoomY;
}

void Camera2D::zoomOut(float factor, const QPointF& anchorWorld, float vpW, float vpH)
{
    zoomIn(1.0f / factor, anchorWorld, vpW, vpH);
}

void Camera2D::pan(float dx, float dy)
{
    panOffset.setX(panOffset.x() + dx);
    panOffset.setY(panOffset.y() + dy);
}

void Camera2D::reset()
{
    zoomX = 1.0f;
    zoomY = 1.0f;
    panOffset = QPointF(0, 0);
}

void Camera2D::zoomToFit(float vpW, float vpH, float sceneW, float sceneH)
{
    if (sceneW <= 0 || sceneH <= 0 || vpW <= 0 || vpH <= 0)
        return;

    float scaleX = vpW / sceneW;
    float scaleY = vpH / sceneH;
    float zoom = std::min(scaleX, scaleY) * 0.9f;

    if (zoom < MIN_ZOOM) zoom = MIN_ZOOM;
    if (zoom > MAX_ZOOM) zoom = MAX_ZOOM;

    zoomX = zoom;
    zoomY = zoom;
    panOffset = QPointF(0, 0);
}

void Camera2D::setViewExtent(float vpW, float vpH, float centerX, float centerY, float halfW, float halfH)
{
    if (halfW <= 0 || halfH <= 0 || vpW <= 0 || vpH <= 0)
        return;

    float targetZoomX = vpW / (2.0f * halfW);
    float targetZoomY = vpH / (2.0f * halfH);
    float zoom = std::min(targetZoomX, targetZoomY);

    if (zoom < MIN_ZOOM) zoom = MIN_ZOOM;
    if (zoom > MAX_ZOOM) zoom = MAX_ZOOM;

    zoomX = zoom;
    zoomY = zoom;
    panOffset = QPointF(-centerX, -centerY);

    SY_TRACEF("Camera2D::setViewExtent: vpW=%.0f, vpH=%.0f, center=(%.2f,%.2f), half=(%.2f,%.2f), zoom=(%.6f,%.6f), pan=(%.2f,%.2f)",
        vpW, vpH, centerX, centerY, halfW, halfH, zoomX, zoomY, panOffset.x(), panOffset.y());
}