#pragma once

#include <QObject>
#include <vector>

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

    std::vector<Eg::EntityId> getSelectedEntityIds() const;
    std::vector<Eg::EntityId> getAllEntityIds() const;
    void notifySceneChanged();
    SceneDocument2D* document() const
    {
        return m_document;
    }

private:
    SceneDocument2D* m_document{ nullptr };
};
