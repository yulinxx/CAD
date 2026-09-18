#pragma once

#include <functional>
#include <memory>

#include "IUiServices.h"
#include "UiServiceGroups.h"

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

namespace Ui
{
    class ViewCaptureService;
}

namespace Eg
{
    class EntityClipboard;
}

/**
 * @struct UiServices
 * @brief UI 服务集合（具体类型聚合，实现 IUIServices 接口）
 *
 * 聚合了 UI 层所需的服务。本结构是**组合根的装配容器**：由
 * ApplicationCompositionRoot 填充，再以聚焦分组 / 接口的形式交付消费者；
 * 消费者之间不再传递本聚合（见下方【已收口】）。
 *
 * 2026-08-31 已删除 4 个死字段：layoutService、layerPersistenceBridge、
 * exportService、settingsService —— 它们只在装配处被赋值，全仓无读取点
 * （settingsService 甚至只被 UiWorkbench 写入一次）。对象所有权都在
 * ApplicationCompositionRoot 的 unique_ptr 上，真正需要它们的
 * FileOperationRegistry 走 FileOperationConfig 单独注入，与本结构无关。
 *
 * 【已收口（P2）】消费者之间不再传递本聚合：
 *  - 只依赖抽象服务的消费者直接依赖对应接口（如 WorkbenchMenuManager → IRecentFileService）
 *  - 需要多个服务的消费者依赖聚焦分组（见 UiServiceGroups.h：UiState/Command/Scene/
 *    Persistence/View）
 *  - 工作台接口 UiWorkbench::initialize 收 WorkbenchServices bundle
 * 本聚合仅由 ApplicationCompositionRoot 填充，作为**装配容器**使用。
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

    /// 视图导出服务（「导出视图」命令；2D/3D 共用一个实例）
    ///
    /// 与 importService 同理放这里：消费者是工作台层（2D 的视图命令注册、
    /// 3D 的导出视图操作），而它们拿不到 ApplicationCompositionRoot。
    Ui::ViewCaptureService* captureService{ nullptr };

    // ---- 聚焦分组（供只依赖部分服务的消费者使用）----
    // 由装配处按需取出并传给消费者，避免消费者依赖整个聚合。
    UiStateServices uiState() const
    {
        return { stateCenter, interactionDispatcher };
    }
    CommandServices commands() const
    {
        return { operationBus, undoManager };
    }
    SceneServices scene() const
    {
        return { selectionService, document2D, layerManager, layerManagerBridge,
                 layerEditService, sceneEditService, clipboard };
    }
    PersistenceServices persistence() const
    {
        return { persistenceService, importService, recentFileService };
    }
    ViewServices view() const
    {
        return { viewportActionHub, unitManager, captureService };
    }

    /// 工作台装配集合（供 UiWorkbench::initialize 使用，避免其接口依赖扁平聚合）
    WorkbenchServices workbench() const
    {
        return { uiState(), commands(), scene(), persistence(), view() };
    }

    // ---- IUIServices 接口实现 ----

    ISelectionService* getSelectionService() const override { return selectionService; }
    IUndoRedoManager* getUndoManager() const override { return undoManager; }
    IInteractionDispatcher* getInteractionDispatcher() const override { return interactionDispatcher; }
};