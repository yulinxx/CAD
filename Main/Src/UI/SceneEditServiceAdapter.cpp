#include "SceneEditServiceAdapter.h"
#include "SceneDocument2D.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Mat/Mat.hpp"

#include <QDateTime>
#include <cmath>
#include <memory>

SceneEditServiceAdapter::SceneEditServiceAdapter(SceneDocument2D* document, QObject* parent)
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
    auto* sm = m_document->sceneManager();
    if (!sm) return result;

    for (auto* entity : sm->getSelectedEntities())
        result.push_back(entity->id);
    return result;
}

std::vector<Eg::EntityId> SceneEditServiceAdapter::getAllEntityIds() const
{
    std::vector<Eg::EntityId> result;
    if (!m_document)
        return result;
    auto* sm = m_document->sceneManager();
    if (!sm) return result;

    for (auto* entity : sm->getAllEntities())
        result.push_back(entity->id);
    return result;
}

void SceneEditServiceAdapter::notifySceneChanged()
{
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

    if (!m_previewActive)
        beginTransform();

    restoreOriginalPositions();

    auto* sm = m_document->sceneManager();
    if (sm)
    {
        for (auto* entity : sm->getSelectedEntities())
            applyTransformToEntity(entity, params);
    }

    m_previewParams = params;
    emit transformPreviewed(params);
}

bool SceneEditServiceAdapter::commitTransform(const TransformParameters& params)
{
    if (!m_document)
        return false;
    auto* sm = m_document->sceneManager();
    if (!sm) return false;

    switch (params.type)
    {
        case TransformType::Move:
        {
            QPointF delta(params.moveX, params.moveY);
            auto mat = Ut::Mat3d::translate(delta.x(), delta.y());
            for (auto* entity : sm->getSelectedEntities())
                entity->transform(mat);
            break;
        }
        case TransformType::Rotate:
        {
            double angleRad = params.rotateAngle * M_PI / 180.0;
            auto mat = Ut::Mat3d::rotate(angleRad);
            auto center = Ut::Vec2d(params.anchorX, params.anchorY);
            for (auto* entity : sm->getSelectedEntities())
            {
                auto t = Ut::Mat3d::translate(-center.x(), -center.y())
                       * mat
                       * Ut::Mat3d::translate(center.x(), center.y());
                entity->transform(t);
            }
            break;
        }
        case TransformType::Mirror:
        {
            for (auto* entity : sm->getSelectedEntities())
            {
                if (entity->eType == Eg::EType::LINE)
                {
                    auto* line = static_cast<Eg::SyLine*>(entity);
                    if (params.mirrorAxis == 0)
                    {
                        line->vPoints[0].y() = -line->vPoints[0].y();
                        line->vPoints[1].y() = -line->vPoints[1].y();
                    }
                    else if (params.mirrorAxis == 1)
                    {
                        line->vPoints[0].x() = -line->vPoints[0].x();
                        line->vPoints[1].x() = -line->vPoints[1].x();
                    }
                }
                else if (entity->eType == Eg::EType::CIRCLE)
                {
                    if (params.mirrorAxis == 0)
                        entity->basePoint.y() = -entity->basePoint.y();
                    else if (params.mirrorAxis == 1)
                        entity->basePoint.x() = -entity->basePoint.x();
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
    auto* sm = m_document->sceneManager();
    if (!sm) return;

    for (auto* entity : sm->getSelectedEntities())
    {
        QPointF center;
        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            center = QPointF((line->vPoints[0].x() + line->vPoints[1].x()) * 0.5,
                            (line->vPoints[0].y() + line->vPoints[1].y()) * 0.5);
        }
        else
        {
            center = QPointF(entity->basePoint.x(), entity->basePoint.y());
        }
        m_originalPositions[QString::number(entity->id)] = center;
    }
}

void SceneEditServiceAdapter::restoreOriginalPositions()
{
    if (!m_document || m_originalPositions.isEmpty())
        return;
    auto* sm = m_document->sceneManager();
    if (!sm) return;

    for (auto* entity : sm->getSelectedEntities())
    {
        QString id = QString::number(entity->id);
        if (!m_originalPositions.contains(id))
            continue;

        QPointF originalCenter = m_originalPositions[id];
        QPointF currentCenter;
        if (entity->eType == Eg::EType::LINE)
        {
            auto* line = static_cast<Eg::SyLine*>(entity);
            currentCenter = QPointF((line->vPoints[0].x() + line->vPoints[1].x()) * 0.5,
                                    (line->vPoints[0].y() + line->vPoints[1].y()) * 0.5);
        }
        else
        {
            currentCenter = QPointF(entity->basePoint.x(), entity->basePoint.y());
        }

        QPointF delta = originalCenter - currentCenter;
        auto mat = Ut::Mat3d::translate(delta.x(), delta.y());
        entity->transform(mat);
    }
}

void SceneEditServiceAdapter::clearOriginalPositions()
{
    m_originalPositions.clear();
}

void SceneEditServiceAdapter::applyTransformToEntity(Eg::SyEntity* entity, const TransformParameters& params)
{
    if (!entity)
        return;
    auto* sm = m_document ? m_document->sceneManager() : nullptr;
    if (!sm) return;

    switch (params.type)
    {
        case TransformType::Move:
        {
            QPointF delta(params.moveX, params.moveY);
            entity->transform(Ut::Mat3d::translate(delta.x(), delta.y()));
            break;
        }
        case TransformType::Rotate:
        {
            double angleRad = params.rotateAngle * M_PI / 180.0;
            auto center = Ut::Vec2d(params.anchorX, params.anchorY);
            auto t = Ut::Mat3d::translate(-center.x(), -center.y())
                   * Ut::Mat3d::rotate(angleRad)
                   * Ut::Mat3d::translate(center.x(), center.y());
            entity->transform(t);
            break;
        }
        case TransformType::Mirror:
        {
            if (entity->eType == Eg::EType::LINE)
            {
                auto* line = static_cast<Eg::SyLine*>(entity);
                if (params.mirrorAxis == 0)
                {
                    line->vPoints[0].y() = -line->vPoints[0].y();
                    line->vPoints[1].y() = -line->vPoints[1].y();
                }
                else if (params.mirrorAxis == 1)
                {
                    line->vPoints[0].x() = -line->vPoints[0].x();
                    line->vPoints[1].x() = -line->vPoints[1].x();
                }
            }
            else if (entity->eType == Eg::EType::CIRCLE)
            {
                if (params.mirrorAxis == 0)
                    entity->basePoint.y() = -entity->basePoint.y();
                else if (params.mirrorAxis == 1)
                    entity->basePoint.x() = -entity->basePoint.x();
            }
            break;
        }
        default:
            break;
    }
}
