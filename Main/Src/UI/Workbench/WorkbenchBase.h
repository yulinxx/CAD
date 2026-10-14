#pragma once

#include "UiWorkbench.h"
#include "ConnectionGuard.h"
#include "WorkbenchTiming.h"

#include <cstddef>
#include <cstdint>

class QMenu;
class QTimer;
class StatusBarBase;
class WorkbenchWindow;

class WorkbenchBase : public UiWorkbench
{
    Q_OBJECT

public:
    explicit WorkbenchBase(QObject* parent = nullptr);
    ~WorkbenchBase() override;

    bool requiresSkeletonDocks() const override;
    bool managesOwnMenus() const override;

protected:
    ConnectionGuard& connections();

    void mountStatusBarWidget(StatusBarBase* statusBar);
    void unmountStatusBarWidget();

    QMenu* buildContextMenu(const QString& contextMenuId, bool hasSelection);

    void schedulePropertiesPanelRefresh();
    void cleanupPropertiesPanelRefreshTimer();

    void scheduleSceneTreeRefresh();
    void cleanupSceneTreeRefreshTimer();

    void setSceneTreeRefreshInterval(int intervalMs);
    int sceneTreeRefreshInterval() const;

    void setPropertiesRefreshInterval(int intervalMs);
    int propertiesRefreshInterval() const;

    void resetSceneTreeState();

    /// 属性面板重建钩子：子类接入各自的属性面板实现（默认空）
    virtual void refreshPropertiesPanel() {}

    /// 场景树增量刷新钩子：子类接入各自的场景树实现（默认空）
    virtual void applySceneTreeIncremental(const char* src = nullptr)
    {
        Q_UNUSED(src)
    }

protected:
    ConnectionGuard m_connections;

    QTimer* m_sceneTreeRefreshTimer{ nullptr };
    QTimer* m_propertiesRefreshTimer{ nullptr };

    int m_sceneTreeRefreshIntervalMs{ WorkbenchTiming::kSceneTreeDebounceMs };
    int m_propertiesRefreshIntervalMs{ WorkbenchTiming::kPropertiesDebounceMs };

    std::size_t m_lastSceneTreeEntityCount{ 0 };
    uint64_t m_lastSceneTreeStructureRevision{ 0 };
    uint64_t m_sceneTreeCursor{ 0 };
    bool m_sceneTreeIncrementalBusy{ false };
};
