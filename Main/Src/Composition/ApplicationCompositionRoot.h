#pragma once

#include <memory>
#include <vector>

#include "UI/Interaction/UiInteractionDispatcher.h"
#include "UI/Services/UiLayoutService.h"
#include "UI/Services/UiShellHost.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/UiThemeService.h"
#include "UI/Documents/SceneDocument2D.h"
#include "UI2D/Operation/OperationBus.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Edit/LayerEditService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"
#include "Engine3D/SceneManager3D.h"

#include "FileIO/FileIOManager.h"

class PersistenceService;
class LayerPersistenceBridge;
class ImportService;
class ImportDispatcher;
class ExportService;
class ExportDispatcher;
class FileDialogService;
class RecentFileService;
class HelpDialogService;
class FileOperationRegistry;

/**
 * @class ApplicationCompositionRoot
 * @brief 应用程序组合根类
 *
 * 负责创建和组装所有核心服务，包括状态中心、主题服务、
 * 布局服务、命令分发器和 UI Shell 宿主。
 */
class ApplicationCompositionRoot
{
public:
    ApplicationCompositionRoot();
    ~ApplicationCompositionRoot();

public:
    /// 获取 UI Shell 宿主
    UiShellHost* shellHost();

    /// 获取状态中心
    UiStateCenter* stateCenter();

    /// 获取主题服务
    UiThemeService* themeService();

    /// 获取布局服务
    UiLayoutService* layoutService();

    /// 获取交互式命令生命周期分发器
    IInteractionDispatcher* interactionDispatcher();

    /// 获取操作总线
    OperationBus* operationBus();

    /// 获取撤销重做管理器
    IUndoRedoManager* undoRedoManager();

    /// 获取图层管理器
    LayerManager* layerManager();

    /// 获取图层管理器 Qt 桥接
    QtLayerManagerBridge* layerManagerBridge();

    /// 获取图层编辑服务
    LayerEditService* layerEditService();

    /// 获取图层持久化桥接器
    LayerPersistenceBridge* layerPersistenceBridge();

    /// 获取持久化服务
    PersistenceService* persistenceService();

    /// 获取导入服务
    ImportService* importService()
    {
        return m_importService.get();
    }

    /// 获取导入分发器
    ImportDispatcher* importDispatcher()
    {
        return m_importDispatcher.get();
    }

    /// 获取导出服务
    ExportService* exportService()
    {
        return m_exportService.get();
    }

    /// 获取2D场景文档
    SceneDocument2D* document2D()
    {
        return m_document2D.get();
    }

    /// 获取导出分发器
    ExportDispatcher* exportDispatcher()
    {
        return m_exportDispatcher.get();
    }

    /// 获取文件对话框服务
    FileDialogService* fileDialogService()
    {
        return m_fileDialogService.get();
    }

    /// 获取最近文件服务
    RecentFileService* recentFileService()
    {
        return m_recentFileService.get();
    }

    /// 获取帮助弹窗服务
    HelpDialogService* helpDialogService()
    {
        return m_helpDialogService.get();
    }

    /// 获取3D场景管理器
    Eg::SceneManager3D* sceneManager3D()
    {
        return m_sceneManager3D.get();
    }

    /// 获取场景管理器
    Eg::SceneManager* sceneManager()
    {
        return m_sceneManager.get();
    }

    /// 获取场景编辑服务
    SceneEditService* sceneEditService()
    {
        return m_sceneEditService.get();
    }

private:
    /// 注册帮助操作（About / Settings / Docs / Shortcuts）
    void registerHelpOperations();

    /// 注册缺失的工具切换操作（提示暂未实现）
    void registerPendingToolOperations();

    /// 注册缺失的算法/编辑操作（提示暂未实现）
    void registerPendingAlgorithmOperations();

    /// 保存文档持久化记录
    void saveDocumentPersistenceRecord(const QString& filePath, int entityCount);

private:
    /// UI Shell 宿主
    std::unique_ptr<UiShellHost> m_shellHost;

    /// UI 状态中心
    std::unique_ptr<UiStateCenter> m_stateCenter;

    /// 主题服务
    std::unique_ptr<UiThemeService> m_themeService;

    /// 布局服务
    std::unique_ptr<UiLayoutService> m_layoutService;

    /// 交互式命令生命周期分发器
    std::unique_ptr<IInteractionDispatcher> m_interactionDispatcher;

    /// 操作总线（新命令主线）
    std::unique_ptr<OperationBus> m_operationBus;

    /// 场景管理器（新系统核心）
    std::unique_ptr<Eg::SceneManager> m_sceneManager;

    /// 3D 场景管理器
    std::unique_ptr<Eg::SceneManager3D> m_sceneManager3D;

    /// 撤销重做管理器（新系统）
    std::unique_ptr<UndoRedoManager> m_undoRedoManager;

    /// 场景编辑服务（新系统）
    std::unique_ptr<SceneEditService> m_sceneEditService;

    /// 2D 场景文档（依赖 SceneEditService）
    std::unique_ptr<SceneDocument2D> m_document2D;

    /// 图层管理器（管理图层创建/删除/属性/图元关联）
    std::unique_ptr<LayerManager> m_layerManager;

    /// 图层管理器 Qt 桥接（将观察者回调转为 Qt 信号）
    std::unique_ptr<QtLayerManagerBridge> m_layerManagerBridge;

    /// 图层编辑服务（带 Undo 的图层操作入口）
    std::unique_ptr<LayerEditService> m_layerEditService;

    /// 图层持久化桥接器（将运行态图层变更同步写入数据库）
    std::unique_ptr<LayerPersistenceBridge> m_layerPersistenceBridge;

    /// 文件IO管理器（导入/导出底层库）
    std::unique_ptr<Fio::FileIOManager> m_fileIOManager;

    /// 导入服务（高层导入入口）
    std::unique_ptr<ImportService> m_importService;
    /// 导入分发器（管理格式读取器注册）
    std::unique_ptr<ImportDispatcher> m_importDispatcher;
    /// 导出服务（高层导出入口）
    std::unique_ptr<ExportService> m_exportService;
    /// 导出分发器（管理格式写入器注册）
    std::unique_ptr<ExportDispatcher> m_exportDispatcher;

    /// 文件对话框服务
    std::unique_ptr<FileDialogService> m_fileDialogService;

    /// 最近文件服务
    std::unique_ptr<RecentFileService> m_recentFileService;

    /// 帮助弹窗服务
    std::unique_ptr<HelpDialogService> m_helpDialogService;

    /// 持久化服务（非拥有指针，由 AppInitializer 管理生命周期）
    PersistenceService* m_persistenceService{ nullptr };

    /// 文件操作注册表
    std::unique_ptr<FileOperationRegistry> m_fileOperationRegistry;
};
