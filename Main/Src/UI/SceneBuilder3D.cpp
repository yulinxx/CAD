#include "SceneBuilder3D.h"

#include "UiEntities.h"

std::shared_ptr<SceneDocument3D> SceneBuilder3D::createDefaultScene(QString& rootNodeId)
{
    auto scene = std::make_shared<SceneDocument3D>();

    auto root = scene->createNode(defaultRootNodeName());
    auto mesh = scene->createNode(QStringLiteral("Mesh"));
    auto childA = scene->createNode(QStringLiteral("Child A"));
    auto childB = scene->createNode(QStringLiteral("Child B"));

    mesh->addChild(childA);
    mesh->addChild(childB);
    root->addChild(mesh);
    scene->selection().add(root);
    rootNodeId = root->id();

    return scene;
}

QString SceneBuilder3D::defaultRootNodeName()
{
    return QStringLiteral("Root");
}
