#pragma once

#include <functional>
#include <memory>

#include "IUiServices.h"

class IInteractionDispatcher;
class UiLayoutService;
class UiStateCenter;
class ISelectionService;
class IUndoRedoManager;
class OperationBus;
class LayerManager;
class QtLayerManagerBridge;
class LayerEditService;
class PersistenceService;
class LayerPersistenceBridge;
class ImportService;
class ExportService;
class RecentFileService;
class SettingsService;
class ViewportActionHub;
class UnitManager;

namespace Eg
{
    class EntityClipboard;
}

/**
 * @struct UiServices
 * @brief UI 服务集合（具体类型聚合，实现 IUiServices 接口）
 *
 * 聚合了 UI 层所需的所有服务。当前暴露 17 个具体类型指针，
 * 违反"UI 只保留入口、交互和状态同步"原则。
 *
 * 【已知问题】消费者直接依赖具体实现类，无法独立测试或替换实现。
 * 其中 ISelectionService、IUndoRedoManager、IInteractionDispatcher
 * 已有抽象接口，其余服务待逐步定义接口后迁移。
 *
 * 【迁移方向】仅依赖抽象服务的消费者应改为依赖 IUiServices；
 * 需要具体服务的消费者应通过独立参数注入。
 */
struct UiServices : public IUiServices
{
    /// UI 状态中心
    UiStateCenter* stateCenter{ nullptr };

    /// 布局服务
    UiLayoutService* layoutService{ nullptr };

    /// 交互式命令生命周期分发器
    IInteractionDispatcher* interactionDispatcher{ nullptr };

    /// 操作总线（新操作主线）
    OperationBus* operationBus{ nullptr };

    /// 撤销重做管理器
    IUndoRedoManager* undoManager{ nullptr };

    /// 选择服务（选择状态与文档事实分离）
    ISelectionService* selectionService{ nullptr };

    /// 2D 场景管理器（命令系统通过此入口操作 2D 图元）
    class SceneDocument2D* document2D{ nullptr };

    /// 图层管理器（管理图层创建/删除/属性/图元关联）
    LayerManager* layerManager{ nullptr };

    /// 图层管理器 Qt 桥接（将观察者回调转为 Qt 信号）
    QtLayerManagerBridge* layerManagerBridge{ nullptr };

    /// 图层编辑服务（带 Undo 的图层操作入口）
    LayerEditService* layerEditService{ nullptr };

    /// 持久化服务（数据库仓储入口，UI 不直接拼 SQL）
    PersistenceService* persistenceService{ nullptr };

    /// 图层持久化桥接器（将 LayerManager 运行态变更同步写入数据库）
    LayerPersistenceBridge* layerPersistenceBridge{ nullptr };

    /// 导入服务（文件导入总入口）
    ImportService* importService{ nullptr };

    /// 导出服务（文件导出总入口）
    ExportService* exportService{ nullptr };

    /// 场景编辑服务（带Undo的图元操作入口，阶段1收口：不再暴露底层 SceneManager）
    class SceneEditService* sceneEditService{ nullptr };

    /// 最近文件服务（统一管理最近文件列表的读写）
    RecentFileService* recentFileService{ nullptr };

    /// 共享 SettingsService singleton（app-level 共享，非每工作台私有）
    SettingsService* settingsService{ nullptr };

    /// 视口动作中枢（视图缩放/平移/重置 → 当前活动视口）
    ViewportActionHub* viewportActionHub{ nullptr };

    /// 单位管理器（显示单位 / 算法对话框单位换算）
    UnitManager* unitManager{ nullptr };

    /// 图元剪贴板（Copy/Cut/Paste 的图元副本缓存）
    Eg::EntityClipboard* clipboard{ nullptr };

    /// 最近文件回调：当文件被打开时调用，参数为文件完整路径
    /// 由 WorkbenchWindow 注入，用于刷新最近文件菜单
    std::function<void(const QString&)> recentFileOpenedCallback;

    // ---- IUiServices 接口实现 ----

    ISelectionService* getSelectionService() const override { return selectionService; }
    IUndoRedoManager* getUndoManager() const override { return undoManager; }
    IInteractionDispatcher* getInteractionDispatcher() const override { return interactionDispatcher; }
};
