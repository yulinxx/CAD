#include "SceneBuilder2D.h"
#include "SceneDocument2D.h"

SceneBuilder2D::DefaultSceneResult SceneBuilder2D::createDefaultScene()
{
    DefaultSceneResult result;
    result.document = std::make_unique<SceneDocument2D>();

    result.primaryLineId = createDemoLine(*result.document, QPointF(-120, -80), QPointF(160, 100));
    result.secondaryLineId = createDemoLine(*result.document, QPointF(-160, 120), QPointF(100, 180));

    return result;
}

QString SceneBuilder2D::createDemoLine(
    SceneDocument2D& doc,
    const QPointF& p1,
    const QPointF& p2)
{
    auto id = doc.createLine(p1, p2);
    doc.selectEntity(id);
    return id;
}
