#pragma once

/**
 * @file UiServiceGroups.h
 * @brief UiServices 的聚焦分组（按职责内聚）
 *
 * 目的：让"只依赖部分服务"的消费者依赖**聚焦分组**，而不是整个 UiServices 聚合，
 * 降低 UI 与实现细节的耦合（见 `UiServices.h` 的收口说明）。
 *
 * 分组只做"指针归拢"，不改变所有权（所有权仍在 ApplicationCompositionRoot）。
 * UiServices 提供 `uiState()/commands()/scene()/persistence()/view()` 访问器，
 * 由装配处按需取出对应分组传给消费者。
 */

class UiStateCenter;
class IInteractionDispatcher;
class OperationBus;
class IUndoRedoManager;
class ISelectionService;
class LayerManager;
class QtLayerManagerBridge;
class LayerEditService;
class PersistenceService;
class ImportService;
class IRecentFileService;
class ViewportActionHub;
class UnitManager;
class SceneEditService;

namespace Ui
{
    class ViewCaptureService;
}

namespace Eg
{
    class EntityClipboard;
}

/// UI 状态与交互：状态中心 + 交互式命令分发
struct UiStateServices
{
    UiStateCenter* stateCenter{ nullptr };
    IInteractionDispatcher* interactionDispatcher{ nullptr };
};

/// 命令与撤销：操作总线 + 撤销重做
struct CommandServices
{
    OperationBus* operationBus{ nullptr };
    IUndoRedoManager* undoManager{ nullptr };
};

/// 2D 场景/图元/图层：选择、文档、图层、编辑、剪贴板
struct SceneServices
{
    ISelectionService* selectionService{ nullptr };
    class SceneDocument2D* document2D{ nullptr };
    LayerManager* layerManager{ nullptr };
    QtLayerManagerBridge* layerManagerBridge{ nullptr };
    LayerEditService* layerEditService{ nullptr };
    SceneEditService* sceneEditService{ nullptr };
    Eg::EntityClipboard* clipboard{ nullptr };
};

/// 持久化 / 导入 / 最近文件
struct PersistenceServices
{
    PersistenceService* persistenceService{ nullptr };
    ImportService* importService{ nullptr };
    IRecentFileService* recentFileService{ nullptr };
};

/// 视图控制 / 单位 / 视图导出
struct ViewServices
{
    ViewportActionHub* viewportActionHub{ nullptr };
    UnitManager* unitManager{ nullptr };
    Ui::ViewCaptureService* captureService{ nullptr };
};
