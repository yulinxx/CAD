#pragma once

#include <QPointF>
#include <QString>

class SceneDocument2D;
class UiStateCenter;

namespace UiSelectionTools
{
    void trimSelectedByPoint(SceneDocument2D* document, const QPointF& point, UiStateCenter* stateCenter);
    void extendSelectedByPoint(SceneDocument2D* document, const QPointF& point, UiStateCenter* stateCenter);
    void applySelectionTransform(SceneDocument2D* document, const QPointF& anchor, const QPointF& target, bool transformCopy, const QString& mode, UiStateCenter* stateCenter, const QString& toolName);
}
