#pragma once

#include <QObject>
#include <QString>
#include <QPointF>
#include <QMap>
#include <vector>
#include <functional>

#include "UI/TransformParameters.h"

class SceneDocument2D;

namespace Eg
{
    using EntityId = int64_t;
    class SyEntity;
    class SceneManager;
}

class SceneEditServiceAdapter : public QObject
{
    Q_OBJECT

public:
    explicit SceneEditServiceAdapter(SceneDocument2D* document, QObject* parent = nullptr);
    ~SceneEditServiceAdapter() override = default;

    void transformEntities(const std::vector<Eg::EntityId>& ids,
        std::function<void()> transform,
        const std::string& description,
        bool group);

    std::vector<Eg::EntityId> getSelectedEntityIds() const;
    std::vector<Eg::EntityId> getAllEntityIds() const;
    void notifySceneChanged();
    void beginTransform();
    void previewTransform(const TransformParameters& params);
    bool commitTransform(const TransformParameters& params);
    void cancelTransform();
    SceneDocument2D* document() const { return m_document; }

signals:
    void transformPreviewed(const TransformParameters& params);
    void transformCommitted(const TransformParameters& params);
    void transformCancelled();

private:
    void saveOriginalPositions();
    void restoreOriginalPositions();
    void clearOriginalPositions();
    void applyTransformToEntity(Eg::SyEntity* entity, const TransformParameters& params);

private:
    SceneDocument2D* m_document{ nullptr };
    TransformParameters m_previewParams;
    bool m_previewActive{ false };
    QMap<QString, QPointF> m_originalPositions;
};
