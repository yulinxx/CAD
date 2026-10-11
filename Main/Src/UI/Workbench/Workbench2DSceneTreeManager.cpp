#include "Workbench2DSceneTreeManager.h"
#include "Workbench2D.h"
#include "WorkbenchWindow.h"
#include "RenderViewport2D.h"
#include "UiSceneTreePanel.h"
#include "SceneTreeBuilder2D.h"
#include "SceneTreeModel2D.h"
#include "UiDockIds.h"
#include "WorkbenchTiming.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"
#include "UI/Services/ISelectionService.h"
#include "UI/Services/UiStateCenter.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine/EntityIdUtils.h"
#include "Engine/SyEntity/SyEntity.h"

#include "Log/SyLogger.h"

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QDockWidget>

// 场景树场景观察者：捕获绕过操作总线的直接编辑（如视口 Delete 键删除），
// 只关心场景变化，由场景树管理器依据图元数量变化判断是否需要重建树。
class SceneTreeSceneObserver2D final : public Eg::SceneNotifier::IObserver
{
public:
    using Callback = std::function<void()>;

    explicit SceneTreeSceneObserver2D(Callback cb)
        : m_cb(std::move(cb))
    {
    }

    void onSceneChanged() override
    {
        if (m_cb)
        {
            m_cb();
        }
    }

private:
    Callback m_cb;
};

Workbench2DSceneTreeManager::Workbench2DSceneTreeManager() = default;
Workbench2DSceneTreeManager::~Workbench2DSceneTreeManager() = default;

void Workbench2DSceneTreeManager::setup(WorkbenchWindow& window, RenderViewport2D* viewport, OperationBus* bus,
    SceneEditService* editService, ISelectionService* selectionService,
    UiStateCenter* stateCenter, std::function<void()> refreshCommandUiState)
{
    m_viewport = viewport;
    m_bus = bus;
    m_editService = editService;
    m_selectionService = selectionService;
    m_stateCenter = stateCenter;
    m_refreshCommandUiState = refreshCommandUiState;

    m_panel = window.sceneTreeDock();
    if (!m_panel || qobject_cast<SceneTreePanel*>(m_panel) == nullptr)
    {
        auto* panel = new SceneTreePanel(&window);
        panel->setObjectName(QStringLiteral("SceneTreeDock"));
        auto* dock = window.registerDockWidget(QObject::tr("Scene"), panel, Qt::LeftDockWidgetArea);
        if (dock)
        {
            dock->setObjectName(UiDockIds::sceneQString());
            dock->setMinimumWidth(180);
            dock->setMaximumWidth(300);
        }
        m_panel = panel;
    }

    auto* panel = m_panel;

    QObject::connect(panel, &SceneTreePanel::selectionChanged, this, [this](const QStringList& ids) {
        applySceneTreeSelection(ids);
    });
    QObject::connect(panel, &SceneTreePanel::visibilityToggled, this, [this](const QString& id, bool visible) {
        toggleEntityVisibility(id, visible);
    });
    QObject::connect(panel, &SceneTreePanel::renameRequested, this, [this](const QString& id, const QString& name) {
        renameEntity(id, name);
    });
    QObject::connect(panel, &SceneTreePanel::deleteRequested, this, [this](const QStringList& ids) {
        deleteSceneTreeSelection(ids);
    });
    QObject::connect(panel, &SceneTreePanel::batchVisibilityRequested, this, [this](const QVector<qint64>& ids, bool visible) {
        setSceneTreeVisibility(ids, visible);
    });
    QObject::connect(panel, &SceneTreePanel::batchLockRequested, this, [this](const QVector<qint64>& ids, bool locked) {
        setSceneTreeLock(ids, locked);
    });

    if (m_viewport)
    {
        QObject::connect(m_viewport, &RenderViewport2D::selectionChanged, this, [this]() {
            syncSceneTreeSelection();
        });
    }
    if (m_bus)
    {
        QObject::connect(m_bus, &OperationBus::undoStateChanged, this, [this]() {
            QTimer::singleShot(0, this, [this]() {
                applySceneTreeIncremental("undoStateChanged");
            });
        });
        QObject::connect(m_bus, &OperationBus::operationCompleted, this, [this](OperationId, bool success) {
            if (!success)
            {
                return;
            }
            QTimer::singleShot(0, this, [this]() { refreshSceneTreeIfNeeded("opCompleted"); });
        });
    }

    Eg::SceneManager* scene = m_editService ? m_editService->sceneManager() : nullptr;
    if (scene)
    {
        if (!m_refreshTimer)
        {
            m_refreshTimer = new QTimer(this);
            m_refreshTimer->setSingleShot(true);
            m_refreshTimer->setInterval(WorkbenchTiming::kSceneTreeDebounceMs);
            QObject::connect(m_refreshTimer, &QTimer::timeout, this, [this]() {
                applySceneTreeIncremental("timer");
            });
        }
        if (!m_observer)
        {
            m_observer = std::make_unique<SceneTreeSceneObserver2D>([this]() {
                onSceneChanged();
            });
        }
        scene->addObserver(m_observer.get());
    }

    refreshSceneTree();
}

void Workbench2DSceneTreeManager::onSceneChanged()
{
    applySceneTreeIncremental("sceneObserver");
}

void Workbench2DSceneTreeManager::refreshSceneTree()
{
    if (!m_panel)
    {
        return;
    }
    Eg::SceneManager* scene = m_editService ? m_editService->sceneManager() : nullptr;
    const SceneTreeTopology2D topology = SceneTreeBuilder2D::buildTopology(scene);
    m_panel->setMode2D(
        topology,
        [scene](qint64 id, bool isGroup) {
            return SceneTreeBuilder2D::rowMeta(scene, nullptr, SceneTreeRow2D{ id, isGroup });
        },
        [scene](qint64 groupId) {
            return SceneTreeBuilder2D::groupMembers(scene, groupId);
        });

    m_lastEntityCount = scene ? scene->getEntityCount() : 0;
    m_lastStructureRevision = scene ? scene->structureRevision() : 0;
    m_lastTopologyRevision = scene ? scene->groupManager().topologyRevision() : 0;

    m_cursor = scene ? scene->currentRevision() : m_cursor;

    const char* src = m_refreshSource ? m_refreshSource : "misc";
    m_refreshSource = nullptr;
    SY_INFOF("[Workbench2DSceneTree] rebuilt src=%s: topLevel=%d entities=%zu structRev=%llu topoRev=%llu",
        src,
        static_cast<int>(topology.topLevel.size()),
        m_lastEntityCount,
        static_cast<unsigned long long>(m_lastStructureRevision),
        static_cast<unsigned long long>(m_lastTopologyRevision));
}

void Workbench2DSceneTreeManager::refreshSceneTreeIfNeeded(const char* src)
{
    if (!m_panel)
    {
        return;
    }
    Eg::SceneManager* scene = m_editService ? m_editService->sceneManager() : nullptr;
    if (!scene)
    {
        return;
    }

    const std::size_t count = scene->getEntityCount();
    const uint64_t structureRev = scene->structureRevision();
    const uint64_t topologyRev = scene->groupManager().topologyRevision();
    if (count == m_lastEntityCount && structureRev == m_lastStructureRevision &&
        topologyRev == m_lastTopologyRevision)
    {
        return;
    }

    m_lastEntityCount = count;
    m_lastStructureRevision = structureRev;
    m_lastTopologyRevision = topologyRev;

    if (src && !m_refreshSource)
    {
        m_refreshSource = src;
    }

    if (m_refreshTimer)
    {
        m_refreshTimer->start();
    }
    else
    {
        refreshSceneTree();
    }
}

void Workbench2DSceneTreeManager::applySceneTreeIncremental(const char* src)
{
    if (!m_panel || !m_editService)
    {
        return;
    }

    if (m_incrementalBusy)
    {
        QTimer::singleShot(0, this, [this]() { applySceneTreeIncremental("deferred"); });
        return;
    }

    struct BusyGuard
    {
        bool& flag;
        explicit BusyGuard(bool& f) : flag(f) { flag = true; }
        ~BusyGuard() { flag = false; }
    } busyGuard(m_incrementalBusy);

    Eg::SceneManager* scene = m_editService->sceneManager();
    if (!scene)
    {
        return;
    }

    if (m_forceRefresh)
    {
        m_forceRefresh = false;
        if (src)
        {
            m_refreshSource = src;
        }
        refreshSceneTree();
        return;
    }

    if (scene->groupManager().topologyRevision() != m_lastTopologyRevision)
    {
        refreshSceneTreeIfNeeded(src);
        return;
    }

    Eg::SceneChangeSet set;
    if (!scene->readChanges(m_cursor, set))
    {
        m_cursor = scene->currentRevision();
        refreshSceneTreeIfNeeded(src);
        return;
    }
    m_cursor = set.toRevision;

    if (set.changes.empty())
    {
        const std::size_t count = scene->getEntityCount();
        if (count != m_lastEntityCount || scene->structureRevision() != m_lastStructureRevision)
        {
            if (src)
            {
                m_refreshSource = src;
            }
            refreshSceneTree();
        }
        return;
    }

    bool hasTreeRelevant = false;
    for (const Eg::SceneChange& ch : set.changes)
    {
        if (Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Added) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Removed) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::StructureChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::LayerChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::VisibilityChanged) ||
            Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::LockChanged))
        {
            hasTreeRelevant = true;
            break;
        }
    }
    if (!hasTreeRelevant)
    {
        return;
    }

    QVector<SceneTreeRow2D> added;
    added.reserve(static_cast<int>(set.changes.size()));
    bool hasNonAdd = false;
    for (const Eg::SceneChange& ch : set.changes)
    {
        if (!Eg::hasSceneChangeKind(ch.kinds, Eg::SceneChangeKind::Added))
        {
            hasNonAdd = true;
            break;
        }
        if (!scene->findEntityById(ch.entityId))
        {
            hasNonAdd = true;
            break;
        }
        added.push_back({ static_cast<qint64>(ch.entityId), false });
    }

    if (hasNonAdd)
    {
        if (src)
        {
            m_refreshSource = src;
        }
        refreshSceneTree();
        return;
    }

    if (added.isEmpty())
    {
        return;
    }

    m_panel->appendTopLevelRows(added);

    m_lastEntityCount = scene->getEntityCount();
    m_lastStructureRevision = scene->structureRevision();
    m_lastTopologyRevision = scene->groupManager().topologyRevision();

    SY_DEBUGF("[Workbench2DSceneTree] incremental src=%s: +%lld rows entities=%zu structRev=%llu",
        src ? src : "misc",
        added.size(),
        m_lastEntityCount,
        static_cast<unsigned long long>(m_lastStructureRevision));
}

void Workbench2DSceneTreeManager::syncSceneTreeSelection()
{
    if (!m_panel)
    {
        return;
    }
    Eg::SceneManager* scene = m_editService ? m_editService->sceneManager() : nullptr;
    auto selected = SceneTreeBuilder2D::selectedIds(scene);
    m_panel->setSelectedIds(selected);
}

void Workbench2DSceneTreeManager::applySceneTreeSelection(const QStringList& ids)
{
    if (!m_selectionService)
    {
        return;
    }
    if (ids.isEmpty())
    {
        m_selectionService->clear();
        return;
    }

    std::vector<std::string> storage;
    storage.reserve(ids.size());
    for (const QString& id : ids)
    {
        storage.push_back(id.toStdString());
    }
    std::vector<const char*> cids;
    cids.reserve(storage.size());
    for (const std::string& str : storage)
    {
        cids.push_back(str.c_str());
    }
    m_selectionService->selectMultiple(cids.data(), cids.size());
}

void Workbench2DSceneTreeManager::toggleEntityVisibility(const QString& id, bool visible)
{
    Eg::SceneManager* scene = m_editService ? m_editService->sceneManager() : nullptr;
    if (!scene)
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    if (auto* entity = scene->findEntityById(*eid))
    {
        entity->setVisible(visible);
        scene->notifySceneChanged();
        QTimer::singleShot(0, this, [this]() { refreshSceneTree(); });
    }
}

void Workbench2DSceneTreeManager::renameEntity(const QString& id, const QString& newName)
{
    if (newName.isEmpty() || !m_editService)
    {
        return;
    }
    const auto eid = Eg::parseEntityId(id.toStdString());
    if (!eid)
    {
        return;
    }
    Eg::SceneManager* scene = m_editService->sceneManager();
    if (!scene)
    {
        return;
    }

    const std::string name = newName.toStdString();
    m_editService->mutateEntities(
        { *eid },
        [scene, entityId = *eid, name]() {
            if (auto* entity = scene->findSyEntityById(entityId))
            {
                entity->setName(name.c_str());
            }
        },
        "Rename");

    SY_DEBUGF("[Workbench2DSceneTree] renameEntity: id=%lld name=%s", static_cast<long long>(*eid), name.c_str());
    refreshSceneTree();
}

void Workbench2DSceneTreeManager::deleteSceneTreeSelection(const QStringList& ids)
{
    if (ids.isEmpty() || !m_editService)
    {
        return;
    }
    std::vector<Eg::EntityId> eids;
    eids.reserve(ids.size());
    for (const QString& id : ids)
    {
        const auto eid = Eg::parseEntityId(id.toStdString());
        if (eid)
        {
            eids.push_back(*eid);
        }
    }
    if (eids.empty())
    {
        return;
    }
    m_editService->deleteEntities(eids, "Delete from Scene Tree");
    refreshSceneTree();
    syncSceneTreeSelection();
}

void Workbench2DSceneTreeManager::setSceneTreeVisibility(const QVector<qint64>& ids, bool visible)
{
    Eg::SceneManager* scene = m_editService ? m_editService->sceneManager() : nullptr;
    if (!scene || ids.isEmpty())
    {
        return;
    }

    std::vector<Eg::EntityId> entityIds;
    entityIds.reserve(static_cast<size_t>(ids.size()));
    for (qint64 id : ids)
    {
        entityIds.push_back(static_cast<Eg::EntityId>(id));
    }
    scene->setEntitiesVisible(entityIds, visible);

    if (!visible)
    {
        std::vector<Eg::SyEntity*> stillVisible;
        bool hadHiddenSelected = false;
        scene->forEachSelected([&stillVisible, &hadHiddenSelected](Eg::SyEntity* e) {
            if (!e)
            {
                return;
            }
            if (e->visible())
            {
                stillVisible.push_back(e);
            }
            else
            {
                hadHiddenSelected = true;
            }
        });
        if (hadHiddenSelected)
        {
            scene->selectEntities(stillVisible);
        }
    }

    scene->notifySceneChanged();

    if (m_panel)
    {
        m_panel->refreshRows(ids);
    }
}

void Workbench2DSceneTreeManager::setSceneTreeLock(const QVector<qint64>& ids, bool locked)
{
    Eg::SceneManager* scene = m_editService ? m_editService->sceneManager() : nullptr;
    if (!scene || ids.isEmpty())
    {
        return;
    }

    std::vector<Eg::EntityId> entityIds;
    entityIds.reserve(static_cast<size_t>(ids.size()));
    for (qint64 id : ids)
    {
        entityIds.push_back(static_cast<Eg::EntityId>(id));
    }
    scene->setEntitiesLocked(entityIds, locked);

    if (m_refreshCommandUiState)
    {
        m_refreshCommandUiState();
    }
}

void Workbench2DSceneTreeManager::shutdown()
{
    if (m_observer && m_editService)
    {
        if (auto* scene = m_editService->sceneManager())
        {
            scene->removeObserver(m_observer.get());
        }
    }
    m_observer.reset();
    if (m_refreshTimer)
    {
        m_refreshTimer->stop();
        m_refreshTimer->deleteLater();
        m_refreshTimer = nullptr;
    }
    m_lastEntityCount = 0;
    m_lastStructureRevision = 0;
    m_lastTopologyRevision = 0;
    m_panel = nullptr;
}
