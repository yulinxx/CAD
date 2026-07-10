#pragma once

#include <memory>
#include <QString>
#include <QPointF>

class SceneDocument2D;

/**
 * @class SceneBuilder2D
 * @brief 2D 场景构建器
 *
 * 负责创建默认的 2D 场景文档及初始图元。
 * 基于 SceneDocument2D / Eg::SceneManager。
 */
class SceneBuilder2D
{
public:
    struct DefaultSceneResult
    {
        std::unique_ptr<SceneDocument2D> document;
        QString primaryLineId;
        QString secondaryLineId;
    };

    static DefaultSceneResult createDefaultScene();

    static QString createDemoLine(
        SceneDocument2D& doc,
        const QPointF& p1,
        const QPointF& p2);
};
