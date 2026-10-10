#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

class SceneTreePanel;
class SceneTreeSceneObserver2D;
class RenderViewport2D;
class WorkbenchWindow;
class OperationBus;
class SceneEditService;

class UiStateCenter;
class ISelectionService;

/**
 * @brief 2D 场景树管理器
 *
 * 从 Workbench2D 提取，负责场景树面板的所有交互逻辑：
 * - 面板创建与信号连接
 * - 全量/增量刷新
 * - 选择同步
 * - 图元可见性/锁定/重命名/删除
 */
class Workbench2DSceneTreeManager : public QObject
{
    Q_OBJECT
public:
    Workbench2DSceneTreeManager();
    ~Workbench2DSceneTreeManager() override;

    void setup(WorkbenchWindow& window, RenderViewport2D* viewport, OperationBus* bus,
        SceneEditService* editService, ISelectionService* selectionService,
        UiStateCenter* stateCenter, std::function<void()> refreshCommandUiState);

    void onSceneChanged();

    void refreshSceneTree();
    void refreshSceneTreeIfNeeded(const char* src = nullptr);
    void applySceneTreeIncremental(const char* src = nullptr);

    void syncSceneTreeSelection();
    void applySceneTreeSelection(const QStringList& ids);
    void toggleEntityVisibility(const QString& id, bool visible);
    void renameEntity(const QString& id, const QString& newName);
    void deleteSceneTreeSelection(const QStringList& ids);
    void setSceneTreeVisibility(const QVector<qint64>& ids, bool visible);
    void setSceneTreeLock(const QVector<qint64>& ids, bool locked);

    void shutdown();

    SceneTreePanel* panel() const { return m_panel; }

private:
    SceneTreePanel* m_panel{ nullptr };
    std::unique_ptr<SceneTreeSceneObserver2D> m_observer;
    QTimer* m_refreshTimer{ nullptr };
    const char* m_refreshSource{ nullptr };
    uint64_t m_cursor{ 0 };
    bool m_forceRefresh{ false };
    bool m_incrementalBusy{ false };
    std::size_t m_lastEntityCount{ 0 };
    uint64_t m_lastStructureRevision{ 0 };
    uint64_t m_lastTopologyRevision{ 0 };

    RenderViewport2D* m_viewport{ nullptr };
    OperationBus* m_bus{ nullptr };
    SceneEditService* m_editService{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    UiStateCenter* m_stateCenter{ nullptr };
    std::function<void()> m_refreshCommandUiState;

    std::vector<QMetaObject::Connection> m_connections;
};
