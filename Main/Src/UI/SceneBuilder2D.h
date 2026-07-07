#pragma once

#include <memory>
#include <vector>

class EntityDocument2D;
class LineEntity2D;
class QPointF;

/**
 * @class SceneBuilder2D
 * @brief 2D 场景构建器
 *
 * 负责创建默认的 2D 场景文档及初始图元。
 * 将场景构建逻辑从 Workbench2D 中抽离，保持工作台只关注 UI 编排。
 */
class SceneBuilder2D
{
public:
    struct DefaultSceneResult
    {
        std::shared_ptr<EntityDocument2D> document;
        std::shared_ptr<LineEntity2D> primaryLine;
        std::shared_ptr<LineEntity2D> secondaryLine;
    };

    /// 创建一个包含默认演示图元的 2D 场景文档
    static DefaultSceneResult createDefaultScene();

    /// 创建一条演示线段（从 p1 到 p2）
    /// @param doc 目标文档
    /// @param p1 起点
    /// @param p2 终点
    /// @return 创建的线段实体
    static std::shared_ptr<LineEntity2D> createDemoLine(
        EntityDocument2D& doc,
        const QPointF& p1,
        const QPointF& p2);
};
