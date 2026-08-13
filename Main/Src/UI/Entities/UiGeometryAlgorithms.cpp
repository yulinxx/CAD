#include "UiGeometryAlgorithms.h"

#include <algorithm>
#include <QLineF>

namespace UiGeometryAlgorithms
{
    QPointF projectPointToLine(const QPointF& point, const QPointF& a, const QPointF& b)
    {
        const QLineF line(a, b);
        if (line.length() <= 0.0)
        {
            return a;
        }

        const QPointF d = b - a;
        const QPointF ap = point - a;
        const double denom = d.x() * d.x() + d.y() * d.y();
        if (denom <= 0.0)
        {
            return a;
        }

        const double t = std::clamp((ap.x() * d.x() + ap.y() * d.y()) / denom, 0.0, 1.0);
        return QPointF(a.x() + d.x() * t, a.y() + d.y() * t);
    }

    QPointF rotatePoint90(const QPointF& p, const QPointF& anchor)
    {
        const QPointF v = p - anchor;
        return QPointF(anchor.x() - v.y(), anchor.y() + v.x());
    }

    QPointF mirrorPointVertical(const QPointF& p, const QPointF& anchor)
    {
        return QPointF(2.0 * anchor.x() - p.x(), p.y());
    }
}  // namespace UiGeometryAlgorithms