#pragma once

#include <functional>
#include <memory>

#include "IUIServices.h"

class IInteractionDispatcher;
class UiStateCenter;
class ISelectionService;
class IUndoRedoManager;
class OperationBus;
class LayerManager;
class QtLayerManagerBridge;
class LayerEditService;
class PersistenceService;
class ImportService;
class IRecentFileService;
class ViewportActionHub;
class UnitManager;

namespace Eg
{
    class EntityClipboard;
}

/**
 * @struct UiServices
 * @brief UI 服务集合（具体类型聚合，实现 IUIServices 接口）
 *
 * 聚合了 UI 层所需的服务。当前暴露 15 个指针，其中 4 个是抽象接口
 * （ISelectionService / IUndoRedoManager / IInteractionDispatcher /
 * IRecentFileService），其余为具体类型，违反"UI 只保留入口、交互和状态同步"原则。
 *
 * 2026-08-31 已删除 4 个死字段：layoutService、layerPersistenceBridge、
 * exportService、settingsService —— 它们只在装配处被赋值，全仓无读取点
 * （settingsService 甚至只被 UiWorkbench 写入一次）。对象所有权都在
 * ApplicationCompositionRoot 的 unique_ptr 上，真正需要它们的
 * FileOperationRegistry 走 FileOperationConfig 单独注入，与本结构无关。
 *
 * 【已知问题】消费者直接依赖具体实现类，无法独立测试或替换实现。
 *
 * 【迁移方向】仅依赖抽象服务的消费者应改为依赖 IUIServices；
 * 需要具体服务的消费者应通过独立参数注入。读取点集中在 UiWorkbench 与
 * WorkbenchWindow 两处，逐字段下沉为构造参数是可行的下一步。
 */
struct UiServices : public IUIServices
{
    /// UI 状态中心
    UiStateCenter* stateCenter{ nullptr };

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

    /// 导入服务（文件导入总入口）
    ImportService* importService{ nullptr };

    /// 场景编辑服务（带Undo的图元操作入口，阶段1收口：不再暴露底层 SceneManager）
    class SceneEditService* sceneEditService{ nullptr };

    /// 最近文件服务（全仓唯一的最近文件读写入口，实现为 Main 的 RecentFileService）
    IRecentFileService* recentFileService{ nullptr };

    /// 视口动作中枢（视图缩放/平移/重置 → 当前活动视口）
    ViewportActionHub* viewportActionHub{ nullptr };

    /// 单位管理器（显示单位 / 算法对话框单位换算）
    UnitManager* unitManager{ nullptr };

    /// 图元剪贴板（Copy/Cut/Paste 的图元副本缓存）
    Eg::EntityClipboard* clipboard{ nullptr };

    // ---- IUIServices 接口实现 ----

    ISelectionService* getSelectionService() const override { return selectionService; }
    IUndoRedoManager* getUndoManager() const override { return undoManager; }
    IInteractionDispatcher* getInteractionDispatcher() const override { return interactionDispatcher; }
};