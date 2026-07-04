#pragma once

#include <QPointF>
#include <QString>

class EntityDocument2D;
class UiStateCenter;

namespace UiSelectionTools
{
    void trimSelectedByPoint(EntityDocument2D* document, const QPointF& point, UiStateCenter* stateCenter);
    void extendSelectedByPoint(EntityDocument2D* document, const QPointF& point, UiStateCenter* stateCenter);
    void applySelectionTransform(EntityDocument2D* document, const QPointF& anchor, const QPointF& target, bool transformCopy, const QString& mode, UiStateCenter* stateCenter, const QString& toolName);
}
