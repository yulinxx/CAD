#pragma once

#include <QPointF>

#include "Render/RenderTypes.h"

struct Camera2D
{
    float zoomX = 1.0f;
    float zoomY = 1.0f;
    QPointF panOffset;

    static constexpr float MIN_ZOOM = 0.001f;
    static constexpr float MAX_ZOOM = 10000.0f;

    void computeViewMatrix(float outMat[9], float vpW, float vpH) const;

    QPointF screenToWorld(const QPoint& screenPos, float vpW, float vpH) const;

    void zoomIn(float factor, const QPointF& anchorWorld, float vpW, float vpH);
    void zoomOut(float factor, const QPointF& anchorWorld, float vpW, float vpH);

    void pan(float dx, float dy);
    void reset();

    void zoomToFit(float vpW, float vpH, float sceneW, float sceneH);

    void setViewExtent(float vpW, float vpH, float centerX, float centerY, float halfW, float halfH);
};
