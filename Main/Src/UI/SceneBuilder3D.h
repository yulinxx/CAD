#pragma once

#include <memory>
#include <QString>

class SceneDocument3D;

/**
 * @class SceneBuilder3D
 * @brief 3D 场景构建器
 *
 * 负责创建默认的 3D 场景文档及初始节点。
 * 将场景构建逻辑从 Workbench3D 中抽离，保持工作台只关注 UI 编排。
 */
class SceneBuilder3D
{
public:
    /// 创建一个包含默认场景树的 3D 场景文档
    /// @param rootNodeId 输出参数，根节点 ID
    /// @return 初始化的 3D 场景文档
    static std::shared_ptr<SceneDocument3D> createDefaultScene(QString& rootNodeId);

    /// 创建默认场景树节点名称
    static QString defaultRootNodeName();
};
