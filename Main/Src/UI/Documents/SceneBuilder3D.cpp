#include "SceneBuilder3D.h"

#include "UiEntities.h"
#include <QObject>
#include <cstring>

// ABI 安全：返回裸指针（调用方通过 destroyScene 释放）
SceneDocument3DAdapter* SceneBuilder3D::createDefaultScene(QString& rootNodeId)
{
    auto* scene = new SceneDocument3DAdapter();
    auto engineScene = std::make_shared<Eg::SceneManager3D>();
    scene->setEngineScene(engineScene);

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

// ABI 安全：返回裸指针而非 shared_ptr（2026-07-31 P0 修复）
// 内部使用 raw new 创建 SceneDocument3DAdapter，调用方通过
// SceneBuilderBase::destroyScene() 在 DLL 内释放
UI::SceneDocumentBase* SceneBuilder3D::createDefaultScene()
{
    QString dummyId;
    return createDefaultScene(dummyId);
}

size_t SceneBuilder3D::defaultRootName(char* buffer, size_t bufferSize) const
{
    std::string name = defaultRootNodeName().toStdString();
    if (bufferSize == 0 || !buffer)
        return name.size();
    size_t copyLen = (name.size() < bufferSize - 1) ? name.size() : (bufferSize - 1);
    std::memcpy(buffer, name.c_str(), copyLen);
    buffer[copyLen] = '\0';
    return name.size();
}