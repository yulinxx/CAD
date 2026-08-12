#pragma once

#include <functional>
#include <memory>

#include "UiFrameworkServices.h"

class IInteractionDispatcher;
class UiLayoutService;
class UiStateCenter;
class UiThemeService;
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

/**
 * @struct UiServices
 * @brief UI 服务集合
 *
 * 聚合了 UI 层所需的所有服务，包括状态中心、主题服务、
 * 布局服务、命令分发器和撤销栈。通过组合模式统一管理服务依赖。
 */
struct UiServices
{
    /// UI 状态中心
    UiStateCenter* stateCenter{ nullptr };

    /// 主题服务
    UiThemeService* themeService{ nullptr };

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

    /// 最近文件回调：当文件被打开时调用，参数为文件完整路径
    /// 由 WorkbenchWindow 注入，用于刷新最近文件菜单
    std::function<void(const QString&)> recentFileOpenedCallback;

    /// 将框架级桥接信息写入到服务集合中
    /// @param frameworkServices 框架级服务
    /// @return 当前服务集合引用
    UiServices& withFrameworkServices(const UiFrameworkServices& /*frameworkServices*/)
    {
        return *this;
    }
};
