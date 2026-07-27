#pragma once

#include <memory>
#include <QString>
#include <string>

#include "UI/SceneBuilderBase.h"

class SceneDocument3D;

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
    static std::shared_ptr<SceneDocument3D> createDefaultScene(QString& rootNodeId);
    static QString defaultRootNodeName();

    // ---- SceneBuilderBase 接口 ----

    std::shared_ptr<UI::SceneDocumentBase> createDefaultScene() override;
    std::string defaultRootName() const override;
};
