#pragma once

#include <QPointF>

namespace UiGeometryAlgorithms
{
    QPointF projectPointToLine(const QPointF& point, const QPointF& a, const QPointF& b);
    QPointF rotatePoint90(const QPointF& p, const QPointF& anchor);
    QPointF mirrorPointVertical(const QPointF& p, const QPointF& anchor);
}  // namespace UiGeometryAlgorithms
