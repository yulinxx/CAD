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
class AlgorithmApplicationService;
class SettingsService;

namespace Ui
{
    class ViewCaptureService;
}

namespace Eg
{
    class EntityClipboard;
}

/// UI 状态：状态中心
struct UiStateServices
{
    UiStateCenter* stateCenter{ nullptr };
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

/// 算法任务编排（2D 算法应用服务：填充/偏移/阵列/布尔/套料）
///
/// 工作台需要它在算法运行期间禁用工具交互（线性/交互开关），
/// 因此作为显式注入项，而不是让工作台回查组合根实例。
struct AlgorithmServices
{
    AlgorithmApplicationService* algorithmApplicationService{ nullptr };
};

/// 设置服务（2D/3D 共用的唯一设置持久化入口）
///
/// 此前各消费方经 ApplicationCompositionRoot::getSettingsService() 静态取服务，
/// 让组合根同时充当全局注册表。改为显式注入后，消费方不再反向依赖组合根；
/// getSettingsService() 仅保留给「组合根装配之前」的极早期启动路径兜底。
struct SettingsServices
{
    SettingsService* settingsService{ nullptr };
};

/// 工作台装配所需的完整服务集合。
/// 由 UiServices 拆出，作为工作台接口（UiWorkbench::initialize）的入参，
/// 使工作台接口不再依赖扁平的 UiServices 聚合。
struct WorkbenchServices
{
    UiStateServices uiState;
    CommandServices commands;
    SceneServices scene;
    PersistenceServices persistence;
    ViewServices view;
    AlgorithmServices algorithm;
    SettingsServices settings;
};
