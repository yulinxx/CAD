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

    // 验证矩阵：计算视图边界（世界空间）
    float invScaleX = 1.0f / scaleX;
    float invScaleY = 1.0f / scaleY;
    float invTx = -tx / scaleX;
    float invTy = -ty / scaleY;

    float wMinX = -invScaleX + invTx;
    float wMaxX = invScaleX + invTx;
    float wMinY = -invScaleY + invTy;
    float wMaxY = invScaleY + invTy;


    mat3ToArray(view, outMat);
}

Render::Mat3f Camera2D::viewMatrix(float vpW, float vpH) const
{
    float mat[9];
    computeViewMatrix(mat, vpW, vpH);

    Render::Mat3f result;
    for (int i = 0; i < 9; ++i)
        result.data[i] = mat[i];
    return result;
}

QPointF Camera2D::screenToWorld(const QPoint& screenPos, float vpW, float vpH) const
{
    if (vpW <= 0 || vpH <= 0)
        return QPointF(0, 0);

    // 屏幕坐标转换为标准 OpenGL NDC
    float nx = (2.0f * screenPos.x() - vpW) / vpW;
    float ny = (vpH - 2.0f * screenPos.y()) / vpH;

    if (zoomX < 1e-6f || zoomY < 1e-6f)
        return QPointF(nx, ny);

    float scaleX = 2.0f * zoomX / vpW;
    float scaleY = 2.0f * zoomY / vpH;

    float wx = (nx / scaleX) - panOffset.x();
    float wy = (ny / scaleY) - panOffset.y();

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

    // 计算生成的视图边界，用于验证
    float finalScaleX = 2.0f * zoom / vpW;
    float finalScaleY = 2.0f * zoom / vpH;
    float wMinX = -1.0f / finalScaleX;
    float wMaxX = 1.0f / finalScaleX;
    float wMinY = -1.0f / finalScaleY;
    float wMaxY = 1.0f / finalScaleY;

    SY_INFOF("Camera2D::zoomToFit: vp=(%.0f,%.0f), scene=(%.2f,%.2f), zoom=%.6f, scaleX=%.8f, scaleY=%.8f, initialViewBounds=[%.2f,%.2f]-[%.2f,%.2f]",
        vpW, vpH, sceneW, sceneH, zoom, finalScaleX, finalScaleY,
        wMinX, wMinY, wMaxX, wMaxY);
}

void Camera2D::zoomToBBox(float vpW, float vpH,
    float minX, float minY, float maxX, float maxY)
{
    if (vpW <= 0 || vpH <= 0)
        return;

    float sceneW = maxX - minX;
    float sceneH = maxY - minY;

    // 退化场景：给一个默认大小
    if (sceneW <= 0 || sceneH <= 0)
    {
        sceneW = 1000.0f;
        sceneH = 1000.0f;
    }

    float centerX = (minX + maxX) * 0.5f;
    float centerY = (minY + maxY) * 0.5f;

    // 先缩放到边界框尺寸，再平移使中心点对齐
    zoomToFit(vpW, vpH, sceneW, sceneH);
    pan(-centerX, -centerY);

    SY_INFOF("Camera2D::zoomToBBox: bbox=[%.2f,%.2f]-[%.2f,%.2f], center=(%.2f,%.2f), zoom=(%.6f,%.6f), pan=(%.2f,%.2f)",
        minX, minY, maxX, maxY, centerX, centerY, zoomX, zoomY,
        panOffset.x(), panOffset.y());
}

void Camera2D::zoomAtCenter(float factor, float vpW, float vpH)
{
    if (vpW <= 0 || vpH <= 0)
        return;

    // 以视口中心为锚点缩放
    QPoint centerScreen(static_cast<int>(vpW * 0.5f), static_cast<int>(vpH * 0.5f));
    QPointF anchorWorld = screenToWorld(centerScreen, vpW, vpH);
    zoomIn(factor, anchorWorld, vpW, vpH);

    SY_TRACEF("Camera2D::zoomAtCenter: factor=%.3f, vp=(%.0f,%.0f), anchor=(%.2f,%.2f), zoom=(%.6f,%.6f)",
        factor, vpW, vpH, anchorWorld.x(), anchorWorld.y(), zoomX, zoomY);
}

void Camera2D::setViewExtent(float vpW, float vpH,
    float centerX, float centerY, float halfW, float halfH)
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

void Camera2D::resetToDefault(float vpW, float vpH)
{
    // 默认台面范围：1200x800，中心 (600,400)
    constexpr float kTableW = 1200.0f;
    constexpr float kTableH = 800.0f;
    constexpr float kCenterX = kTableW * 0.5f;
    constexpr float kCenterY = kTableH * 0.5f;

    if (vpW > 0 && vpH > 0)
        setViewExtent(vpW, vpH, kCenterX, kCenterY, kCenterX, kCenterY);
    else
        reset();

    SY_INFOF("Camera2D::resetToDefault: vp=(%.0f,%.0f), zoom=(%.6f,%.6f), pan=(%.2f,%.2f)",
        vpW, vpH, zoomX, zoomY, panOffset.x(), panOffset.y());
}
