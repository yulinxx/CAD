#include "SceneBuilder2D.h"

#include "UiEntities.h"

#include <QPointF>

SceneBuilder2D::DefaultSceneResult SceneBuilder2D::createDefaultScene()
{
    DefaultSceneResult result;
    result.document = std::make_shared<EntityDocument2D>();

    result.primaryLine = createDemoLine(*result.document, QPointF(-120, -80), QPointF(160, 100));
    result.secondaryLine = createDemoLine(*result.document, QPointF(-160, 120), QPointF(100, 180));

    return result;
}

std::shared_ptr<LineEntity2D> SceneBuilder2D::createDemoLine(
    EntityDocument2D& doc,
    const QPointF& p1,
    const QPointF& p2)
{
    auto line = doc.createLine(p1, p2);
    if (line)
        doc.selection().add(line);
    return line;
}
