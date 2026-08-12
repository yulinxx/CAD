#pragma once

#include <memory>
#include <QString>
#include <string>

#include "UI/SceneBuilderBase.h"

class SceneDocument3DAdapter;

/**
 * @class SceneBuilder3D
 * @brief 3D 场景构建器
 *
 * 负责创建默认的 3D 场景文档及初始节点。
 * 将场景构建逻辑从 Workbench3D 中抽离，保持工作台只关注 UI 编排。
 */
class SceneBuilder3D : public UI::SceneBuilderBase
{
  public:
    // 返回裸指针（调用方通过 SceneBuilderBase::destroyScene 释放）
    static SceneDocument3DAdapter *createDefaultScene(QString &rootNodeId);
    static QString defaultRootNodeName();

    // ---- SceneBuilderBase 接口 ----

    // SceneBuilderBase override — 返回裸指针，由 caller 管理生命周期
    // 内部委托给静态 createDefaultScene(QString&) 完成实际构建
    UI::SceneDocumentBase *createDefaultScene() override;
    size_t defaultRootName(char *buffer, size_t bufferSize) const override;
};
