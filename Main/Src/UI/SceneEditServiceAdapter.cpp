/**
 * @file SceneEditServiceAdapter.cpp
 * @brief SceneEditService 适配器实现
 */
#include "SceneEditServiceAdapter.h"
#include "UiEntities.h"

#include <QDateTime>
#include <cmath>
#include <memory>

SceneEditServiceAdapter::SceneEditServiceAdapter(EntityDocument2D* document, QObject* parent)
    : QObject(parent)
    , m_document(document)
{
}

void SceneEditServiceAdapter::transformEntities(const std::vector<Eg::EntityId>& ids,
    std::function<void()> transform,
    const std::string& description,
    bool group)
{
    Q_UNUSED(ids);
    Q_UNUSED(description);
    Q_UNUSED(group);

    if (transform)
        transform();
}

std::vector<Eg::EntityId> SceneEditServiceAdapter::getSelectedEntityIds() const
{
    std::vector<Eg::EntityId> result;
    if (!m_document)
        return result;

    for (const auto& entity : m_document->selection().items())
    {
        if (entity)
        {
            Eg::EntityId id = qHash(entity->id());
            result.push_back(id);
        }
    }
    return result;
}

std::vector<Eg::EntityId> SceneEditServiceAdapter::getAllEntityIds() const
{
    std::vector<Eg::EntityId> result;
    if (!m_document)
        return result;

    for (const auto& entity : m_document->entities())
    {
        if (entity)
        {
            Eg::EntityId id = qHash(entity->id());
            result.push_back(id);
        }
    }
    return result;
}

void SceneEditServiceAdapter::notifySceneChanged()
{
    // 过渡期：什么都不做
}

void SceneEditServiceAdapter::beginTransform()
{
    saveOriginalPositions();
    m_previewActive = true;
}

void SceneEditServiceAdapter::previewTransform(const TransformParameters& params)
{
    if (!m_document)
        return;

    // 如果是第一次预览，保存原始位置
    if (!m_previewActive)
    {
        beginTransform();
    }

    // 恢复到原始状态
    restoreOriginalPositions();

    // 应用新变换
    for (const auto& entity : m_document->selection().items())
    {
        if (entity)
        {
            applyTransformToEntity(entity, params);
        }
    }

    m_previewParams = params;
    emit transformPreviewed(params);
}

bool SceneEditServiceAdapter::commitTransform(const TransformParameters& params)
{
    if (!m_document)
        return false;

    // 根据变换类型执行变换
    switch (params.type)
    {
        case TransformType::Move:
        {
            QPointF delta(params.moveX, params.moveY);
            for (const auto& entity : m_document->selection().items())
            {
                if (entity)
                {
                    if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
                        line->translate(delta);
                    else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
                        polyline->translate(delta);
                    else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
                        circle->translate(delta);
                    else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
                        arc->translate(delta);
                }
            }
            break;
        }
        case TransformType::Rotate:
        {
            double angleRad = params.rotateAngle * M_PI / 180.0;
            QPointF center(params.anchorX, params.anchorY);
            double cosA = std::cos(angleRad);
            double sinA = std::sin(angleRad);

            for (const auto& entity : m_document->selection().items())
            {
                if (entity)
                {
                    if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
                        line->rotate(center, cosA, sinA);
                    else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
                        circle->rotate(center, cosA, sinA);
                    else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
                        arc->rotate(center, cosA, sinA);
                }
            }
            break;
        }
        case TransformType::Mirror:
        {
            for (const auto& entity : m_document->selection().items())
            {
                if (entity)
                {
                    if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
                    {
                        if (params.mirrorAxis == 0)
                        {
                            QPointF p1 = line->start();
                            QPointF p2 = line->end();
                            p1.setY(-p1.y());
                            p2.setY(-p2.y());
                            line->setStart(p1);
                            line->setEnd(p2);
                        }
                        else if (params.mirrorAxis == 1)
                        {
                            QPointF p1 = line->start();
                            QPointF p2 = line->end();
                            p1.setX(-p1.x());
                            p2.setX(-p2.x());
                            line->setStart(p1);
                            line->setEnd(p2);
                        }
                    }
                    else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
                    {
                        if (params.mirrorAxis == 0)
                        {
                            QPointF c = circle->center();
                            c.setY(-c.y());
                            circle->setCenter(c);
                        }
                        else if (params.mirrorAxis == 1)
                        {
                            QPointF c = circle->center();
                            c.setX(-c.x());
                            circle->setCenter(c);
                        }
                    }
                }
            }
            break;
        }
        default:
            break;
    }

    clearOriginalPositions();
    m_previewActive = false;
    emit transformCommitted(params);
    return true;
}

void SceneEditServiceAdapter::cancelTransform()
{
    restoreOriginalPositions();
    clearOriginalPositions();
    m_previewActive = false;
    emit transformCancelled();
}

void SceneEditServiceAdapter::saveOriginalPositions()
{
    m_originalPositions.clear();

    if (!m_document)
        return;

    for (const auto& entity : m_document->selection().items())
    {
        if (entity)
        {
            // 保存实体的中心点作为原始位置
            QPointF originalCenter;

            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
            {
                originalCenter = (line->start() + line->end()) / 2.0;
            }
            else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
            {
                originalCenter = circle->center();
            }
            else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
            {
                originalCenter = arc->center();
            }

            m_originalPositions[entity->id()] = originalCenter;
        }
    }
}

void SceneEditServiceAdapter::restoreOriginalPositions()
{
    if (!m_document || m_originalPositions.isEmpty())
        return;

    for (const auto& entity : m_document->selection().items())
    {
        if (entity && m_originalPositions.contains(entity->id()))
        {
            QPointF originalCenter = m_originalPositions[entity->id()];
            QPointF currentCenter;

            // 获取当前中心点
            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
            {
                currentCenter = (line->start() + line->end()) / 2.0;
            }
            else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
            {
                currentCenter = circle->center();
            }
            else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
            {
                currentCenter = arc->center();
            }

            // 计算差值并恢复
            QPointF delta = originalCenter - currentCenter;

            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
            {
                line->translate(delta);
            }
            else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
            {
                polyline->translate(delta);
            }
            else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
            {
                circle->translate(delta);
            }
            else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
            {
                arc->translate(delta);
            }
        }
    }
}

void SceneEditServiceAdapter::clearOriginalPositions()
{
    m_originalPositions.clear();
}

void SceneEditServiceAdapter::applyTransformToEntity(std::shared_ptr<UiEntity> entity, const TransformParameters& params)
{
    if (!entity)
        return;

    switch (params.type)
    {
        case TransformType::Move:
        {
            QPointF delta(params.moveX, params.moveY);
            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
                line->translate(delta);
            else if (auto polyline = std::dynamic_pointer_cast<PolylineEntity2D>(entity))
                polyline->translate(delta);
            else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
                circle->translate(delta);
            else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
                arc->translate(delta);
            break;
        }
        case TransformType::Rotate:
        {
            double angleRad = params.rotateAngle * M_PI / 180.0;
            QPointF center(params.anchorX, params.anchorY);
            double cosA = std::cos(angleRad);
            double sinA = std::sin(angleRad);

            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
                line->rotate(center, cosA, sinA);
            else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
                circle->rotate(center, cosA, sinA);
            else if (auto arc = std::dynamic_pointer_cast<ArcEntity2D>(entity))
                arc->rotate(center, cosA, sinA);
            break;
        }
        case TransformType::Mirror:
        {
            if (auto line = std::dynamic_pointer_cast<LineEntity2D>(entity))
            {
                if (params.mirrorAxis == 0)
                {
                    QPointF p1 = line->start();
                    QPointF p2 = line->end();
                    p1.setY(-p1.y());
                    p2.setY(-p2.y());
                    line->setStart(p1);
                    line->setEnd(p2);
                }
                else if (params.mirrorAxis == 1)
                {
                    QPointF p1 = line->start();
                    QPointF p2 = line->end();
                    p1.setX(-p1.x());
                    p2.setX(-p2.x());
                    line->setStart(p1);
                    line->setEnd(p2);
                }
            }
            else if (auto circle = std::dynamic_pointer_cast<CircleEntity2D>(entity))
            {
                if (params.mirrorAxis == 0)
                {
                    QPointF c = circle->center();
                    c.setY(-c.y());
                    circle->setCenter(c);
                }
                else if (params.mirrorAxis == 1)
                {
                    QPointF c = circle->center();
                    c.setX(-c.x());
                    circle->setCenter(c);
                }
            }
            break;
        }
        default:
            break;
    }
}
