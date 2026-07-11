#include "SceneBuilder3D.h"

#include "UiEntities.h"
#include <QObject>

std::shared_ptr<SceneDocument3D> SceneBuilder3D::createDefaultScene(QString& rootNodeId)
{
    auto scene = std::make_shared<SceneDocument3D>();

    auto root = scene->createNode(defaultRootNodeName().toStdString());
    auto mesh = scene->createNode("Mesh");
    auto childA = scene->createNode("Child A");
    auto childB = scene->createNode("Child B");

    mesh->addChild(childA);
    mesh->addChild(childB);
    root->addChild(mesh);
    scene->selection().add(root);
    rootNodeId = QString::fromStdString(root->id());

    return scene;
}

QString SceneBuilder3D::defaultRootNodeName()
{
    return QObject::tr("Root"); // 根节点
}

std::shared_ptr<UI::SceneDocumentBase> SceneBuilder3D::createDefaultScene()
{
    QString dummyId;
    return createDefaultScene(dummyId);
}

std::string SceneBuilder3D::defaultRootName() const
{
    return defaultRootNodeName().toStdString();
}
