#include "ApplicationCompositionRoot.h"
#include "FileOperationRegistry.h"
#include "CoreOperationRegistry.h"

#include "UI/Services/UiLayoutService.h"
#include "UI/Services/UiServices.h"
#include "UI/Services/UiShellHost.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/UiThemeService.h"
#include "UI/Render/RenderViewport2D.h"
#include "Common/AppInitializer.h"
#include "Persistence/LayerPersistenceBridge.h"
#include "Persistence/PersistenceService.h"

#include "UI2D/Operation/OperationId.h"
#include "UI2D/Operation/IOperation.h"
#include "Log/SyLogger.h"

#include <QApplication>
#include <QFileInfo>
#include <QDateTime>

#include "UI/Services/FileDialogService.h"
#include "UI/Services/RecentFileService.h"
#include "Persistence/Models/DocumentRecord.h"
#include "Persistence/Repositories/DocumentRepository.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "UI/Services/HelpDialogService.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "UI2D/Edit/QtLayerManagerBridge.h"

#include "Import/ImportService.h"
#include "Import/ImportDispatcher.h"
#include "Import/Readers/DxfImportReader.h"
#include "Import/Readers/SvgImportReader.h"
#include "Import/Readers/PdfImportReader.h"
#include "Import/Readers/StepImportReader.h"
#include "Import/Readers/ObjImportReader.h"
#include "Import/Readers/StlImportReader.h"
#include "Import/Readers/PltImportReader.h"
#include "Export/ExportService.h"
#include "Export/ExportDispatcher.h"
#include "Export/Writers/DxfExportWriter.h"
#include "Export/Writers/SvgExportWriter.h"
#include "Export/Writers/PdfExportWriter.h"
#include "Export/Writers/BmpExportWriter.h"
#include "Export/Writers/PngExportWriter.h"
#include "Export/Writers/ObjExportWriter.h"
#include "Export/Writers/StepExportWriter.h"

// 应用组合根组件，负责创建和组装所有核心服务
// 作为依赖注入的中心点，管理UI层和命令系统的生命周期
ApplicationCompositionRoot::~ApplicationCompositionRoot() = default;

// 应用组合根构造函数
// 职责：创建所有核心服务实例，完成依赖注入和信号连接
// 装配顺序：UI 基础服务 → 场景/编辑/图层 → 导入导出 → 对话框服务 → 脏状态同步 → 操作注册
ApplicationCompositionRoot::ApplicationCompositionRoot()
    : m_stateCenter(std::make_unique<UiStateCenter>())
    , m_themeService(std::make_unique<DefaultUiThemeService>())
    , m_layoutService(std::make_unique<DefaultUiLayoutService>())
    , m_interactionDispatcher(std::make_unique<DefaultInteractionDispatcher>())
    , m_shellHost(std::make_unique<UiShellHost>())
    , m_operationBus(std::make_unique<OperationBus>())
    , m_sceneManager(std::make_unique<Eg::SceneManager>())
    , m_sceneManager3D(std::make_unique<Eg::SceneManager3D>())
    , m_undoRedoManager(std::make_unique<UndoRedoManager>(m_sceneManager.get()))
    , m_sceneEditService(std::make_unique<SceneEditService>(m_sceneManager.get(), m_undoRedoManager.get()))
    , m_document2D(std::make_unique<SceneDocument2D>(m_sceneEditService.get()))
    , m_layerManager(std::make_unique<LayerManager>(m_sceneManager.get()))
    , m_layerManagerBridge(std::make_unique<QtLayerManagerBridge>(nullptr))
    , m_layerEditService(std::make_unique<LayerEditService>(m_layerManager.get(), m_undoRedoManager.get(), m_sceneManager.get()))
    , m_fileIOManager(std::make_unique<Fio::FileIOManager>())
    , m_importService(std::make_unique<ImportService>())
    , m_importDispatcher(std::make_unique<ImportDispatcher>())
    , m_exportService(std::make_unique<ExportService>())
    , m_exportDispatcher(std::make_unique<ExportDispatcher>())
{
    // 组装UI服务集合
    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();
    uiServices.layerManager = m_layerManager.get();
    uiServices.layerManagerBridge = m_layerManagerBridge.get();
    uiServices.layerEditService = m_layerEditService.get();
    uiServices.persistenceService = persistenceService();

    // 将 LayerManagerBridge 注册为 LayerManager 的观察者
    m_layerManager->addObserver(m_layerManagerBridge.get());

    // 创建图层持久化桥接器（将运行态图层变更同步写入数据库）
    if (persistenceService() && persistenceService()->isOpen() && persistenceService()->layers())
    {
        m_layerPersistenceBridge = std::make_unique<LayerPersistenceBridge>(
            m_layerManager.get(), persistenceService()->layers());
        m_layerPersistenceBridge->attach();
        uiServices.layerPersistenceBridge = m_layerPersistenceBridge.get();
        SY_INFO("[ApplicationCompositionRoot] LayerPersistenceBridge attached");
    }

    // 将 LayerManager 注入到 SceneEditService，使其在添加图元时自动分配图层
    m_sceneEditService->setLayerManager(m_layerManager.get());

    // 配置UI壳宿主的核心依赖
    m_shellHost->setStateCenter(m_stateCenter.get());
    m_shellHost->setThemeService(m_themeService.get());
    m_shellHost->setOperationBus(m_operationBus.get());

    // ---- 初始化导入/导出服务层 ----
    // 注册导入读取器
    m_importDispatcher->registerReader(std::make_unique<DxfImportReader>());
    m_importDispatcher->registerReader(std::make_unique<SvgImportReader>());
    m_importDispatcher->registerReader(std::make_unique<PdfImportReader>());
    m_importDispatcher->registerReader(std::make_unique<StepImportReader>());
    m_importDispatcher->registerReader(std::make_unique<ObjImportReader>());
    m_importDispatcher->registerReader(std::make_unique<StlImportReader>());
    m_importDispatcher->registerReader(std::make_unique<PltImportReader>());
    // 配置导入服务
    m_importService->setDispatcher(m_importDispatcher.get());
    m_importService->setSceneManager(m_sceneManager.get());
    m_importService->setSceneManager3D(m_sceneManager3D.get());
    m_importService->setEditService(m_sceneEditService.get());
    m_importService->setStateCenter(m_stateCenter.get());
    // 设置文档持久化回调（阶段5回写状态使用）
    m_importService->setDocumentPersistenceCallback([this](const QString& filePath, int entityCount) {
        this->saveDocumentPersistenceRecord(filePath, entityCount);
        });
    // 注册导出写入器
    m_exportDispatcher->registerWriter(std::make_unique<DxfExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<SvgExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<PdfExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<BmpExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<PngExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<ObjExportWriter>());
    m_exportDispatcher->registerWriter(std::make_unique<StepExportWriter>());
    // 配置导出服务
    m_exportService->setDispatcher(m_exportDispatcher.get());
    m_exportService->setSceneManager(m_sceneManager.get());
    m_exportService->setStateCenter(m_stateCenter.get());
    // 将导入/导出服务注入到 UI 服务集合
    uiServices.importService = m_importService.get();
    uiServices.exportService = m_exportService.get();
    m_shellHost->setUiServices(uiServices);

    // 导入进度 → 状态中心
    QObject::connect(m_importService.get(), &ImportService::importStarted,
        m_stateCenter.get(), [this](const QString&) {
            if (m_stateCenter) m_stateCenter->setBusy(true);
        });
    QObject::connect(m_importService.get(), &ImportService::importFinished,
        m_stateCenter.get(), [this](const ImportResult& result) {
            if (!m_stateCenter) return;
            m_stateCenter->setBusy(false);
            QVariantMap meta = m_stateCenter->metadata();
            meta["statusPrompt"] = result.success
                ? QString("Import complete: %1 entities").arg(result.entityCount)
                : QString("Import failed: %1").arg(result.message);
            meta["notificationType"] = result.success ? "info" : "error";
            m_stateCenter->setMetadata(meta);
        });

    // 导出进度 → 状态中心
    QObject::connect(m_exportService.get(), &ExportService::exportStarted,
        m_stateCenter.get(), [this](const QString&) {
            if (m_stateCenter) m_stateCenter->setBusy(true);
        });
    QObject::connect(m_exportService.get(), &ExportService::exportFinished,
        m_stateCenter.get(), [this](const ExportResult& result) {
            if (!m_stateCenter) return;
            m_stateCenter->setBusy(false);
            QVariantMap meta = m_stateCenter->metadata();
            meta["statusPrompt"] = result.success
                ? QString("Export complete: %1 entities").arg(result.exportedEntityCount)
                : QString("Export failed: %1").arg(result.message);
            meta["notificationType"] = result.success ? "info" : "error";
            m_stateCenter->setMetadata(meta);
        });
    // ---- 导入/导出服务层初始化完成 ----

    // ---- UI 对话框服务 ----
    m_fileDialogService = std::make_unique<FileDialogService>();
    m_recentFileService = std::make_unique<RecentFileService>(persistenceService());
    m_helpDialogService = std::make_unique<HelpDialogService>();

    // 脏状态同步：场景变更 → 标记为未保存
    if (m_sceneManager)
    {
        m_sceneManager->setSceneChangedCallback([this]() {
            if (m_stateCenter && !m_stateCenter->dirty())
                m_stateCenter->setDirty(true);
            });
    }

    // 将 OperationBus 设为活动实例，供无上下文调用方使用
    OperationBus::setActiveInstance(m_operationBus.get());

    // 注册各模块操作到 OperationBus
    SY_INFO("[ApplicationCompositionRoot] registering module operations on OperationBus");
    {
        CoreOperationRegistry coreOps(m_operationBus.get(),
            m_sceneEditService.get(),
            m_undoRedoManager.get(),
            m_helpDialogService.get(),
            m_shellHost ? m_shellHost->mainWindow() : nullptr);
        coreOps.registerAll();
    }
    m_fileOperationRegistry = std::make_unique<FileOperationRegistry>(
        m_operationBus.get(),
        m_sceneManager.get(),
        m_importService.get(),
        m_exportService.get(),
        m_fileDialogService.get(),
        m_recentFileService.get(),
        m_helpDialogService.get(),
        m_stateCenter.get(),
        m_layerPersistenceBridge.get(),
        persistenceService(),
        m_shellHost ? m_shellHost->mainWindow() : nullptr);
    m_fileOperationRegistry->registerAll();
    registerHelpOperations();
    registerPendingToolOperations();
    registerPendingAlgorithmOperations();

    SY_INFO("[ApplicationCompositionRoot] initialized with module-based operation registration");
}

UiShellHost* ApplicationCompositionRoot::shellHost()
{
    return m_shellHost.get();
}
UiStateCenter* ApplicationCompositionRoot::stateCenter()
{
    return m_stateCenter.get();
}
UiThemeService* ApplicationCompositionRoot::themeService()
{
    return m_themeService.get();
}
UiLayoutService* ApplicationCompositionRoot::layoutService()
{
    return m_layoutService.get();
}

IInteractionDispatcher* ApplicationCompositionRoot::interactionDispatcher()
{
    return m_interactionDispatcher.get();
}

OperationBus* ApplicationCompositionRoot::operationBus()
{
    return m_operationBus.get();
}

IUndoRedoManager* ApplicationCompositionRoot::undoRedoManager()
{
    return m_undoRedoManager.get();
}

LayerManager* ApplicationCompositionRoot::layerManager()
{
    return m_layerManager.get();
}

QtLayerManagerBridge* ApplicationCompositionRoot::layerManagerBridge()
{
    return m_layerManagerBridge.get();
}

LayerEditService* ApplicationCompositionRoot::layerEditService()
{
    return m_layerEditService.get();
}

LayerPersistenceBridge* ApplicationCompositionRoot::layerPersistenceBridge()
{
    return m_layerPersistenceBridge.get();
}

PersistenceService* ApplicationCompositionRoot::persistenceService()
{
    // 从 AppInitializer 获取已初始化的持久化服务
    if (!m_persistenceService)
        m_persistenceService = AppInitializer::persistenceService();
    return m_persistenceService;
}

// 注册工具切换操作
// 通过 OperationBus 连接到 RenderViewport2D 的工具系统
// 工具名称映射：OperationId -> ToolManager 中的工具名称
namespace
{
    QString toolNameFromOperationId(OperationId opId)
    {
        switch (opId)
        {
            case OperationId::Tool_Select:       return "SelectTool";
            case OperationId::Tool_Point:        return "PointTool";
            case OperationId::Tool_Line:         return "LineTool";
            case OperationId::Tool_Rectangle:    return "RectangleTool";
            case OperationId::Tool_Ellipse:      return "EllipseTool";
            case OperationId::Tool_Circle:       return "CircleTool";
            case OperationId::Tool_Triangle:     return "TriangleTool";
            case OperationId::Tool_Arc:          return "ArcTool";
            case OperationId::Tool_Polygon:      return "PolygonTool";
            case OperationId::Tool_Spline:       return "SplineTool";
            case OperationId::Tool_Text:         return "TextInputTool";
            case OperationId::Tool_Bitmap:       return "BitmapInputTool";
            case OperationId::Tool_QRCode:       return "QRCodeInputTool";
            default:
                SY_WARNF("[ToolOperation] Unmapped OperationId=%d in toolNameFromOperationId",
                    static_cast<int>(opId));
                return QString();
        }
    }

    // 查找当前活动窗口的 2D 视口
    // 链路：QApplication::activeWindow() → WorkbenchWindow → centralWidget() → RenderViewport2D
    // 用于工具切换操作的兜底查找（快速路径在 CommandActionHubActions2D 中直接调用）
    RenderViewport2D* findActiveViewport2D()
    {
        // 通过全局获取当前活动窗口的 2D 视口
        QWidget* activeWindow = QApplication::activeWindow();
        if (!activeWindow)
        {
            SY_DEBUGF("[ToolOperation] No active window found");
            return nullptr;
        }

        auto* workbenchWindow = qobject_cast<WorkbenchWindow*>(activeWindow);
        if (!workbenchWindow)
        {
            SY_DEBUGF("[ToolOperation] Active window is not WorkbenchWindow: %s",
                activeWindow->metaObject()->className());
            return nullptr;
        }

        auto* viewport = qobject_cast<RenderViewport2D*>(workbenchWindow->centralWidget());
        if (!viewport)
        {
            SY_DEBUGF("[ToolOperation] Central widget is not RenderViewport2D");
        }
        return viewport;
    }
}

void ApplicationCompositionRoot::registerPendingToolOperations()
{
    if (!m_operationBus)
        return;

    auto& reg = m_operationBus->registry();

    // 工具操作列表：已接入 ToolManager 的工具
    const OperationId toolOps[] = {
        OperationId::Tool_Select,
        OperationId::Tool_Point,
        OperationId::Tool_Line,
        OperationId::Tool_Rectangle,
        OperationId::Tool_Ellipse,
        OperationId::Tool_Circle,
        OperationId::Tool_Triangle,
        OperationId::Tool_Arc,
        OperationId::Tool_Polygon,
        OperationId::Tool_Spline,
        OperationId::Tool_Text,
        OperationId::Tool_Bitmap,
        OperationId::Tool_QRCode,
    };

    int registered = 0;
    for (const auto& opId : toolOps)
    {
        if (!reg.has(opId))
        {
            QString toolName = toolNameFromOperationId(opId);
            reg.registerOperation(std::make_unique<LambdaOperation>(
                opId, [toolName] {
                    auto* viewport = findActiveViewport2D();
                    if (viewport)
                    {
                        viewport->setActiveTool(toolName);
                        SY_DEBUGF("[ToolOperation] Activated tool: %s", qPrintable(toolName));
                    }
                    else
                    {
                        SY_WARNF("[ToolOperation] No active 2D viewport found for tool: %s",
                            qPrintable(toolName));
                    }
                }));
            registered++;
        }
    }

    if (registered > 0)
    {
        SY_INFOF("[Composition] Registered %d tool operations (fast path -> ToolManager)", registered);
    }
}

// 注册尚未接入的算法/编辑/视图操作
// 这些操作在旧框架中通过 AlgorithmRunner 注册，新框架暂未接入对应服务
// 占位策略：注册 LambdaOperation 打印警告，避免菜单/工具栏点击时静默无响应
// 清理标准：接入真实实现后从此函数移除；长期不实现的应从 OperationId 枚举中删除
void ApplicationCompositionRoot::registerPendingAlgorithmOperations()
{
    if (!m_operationBus)
        return;

    auto& reg = m_operationBus->registry();

    // ---- 算法操作占位（待接入 AlgorithmApplicationService）----
    const OperationId algoOps[] = {
        OperationId::Algo_Fill,
        OperationId::Algo_Nesting,
        OperationId::Algo_Offset,
        OperationId::Algo_Array,
        OperationId::Algo_BooleanUnion,
        OperationId::Algo_BooleanIntersection,
        OperationId::Algo_BooleanDifference,
        OperationId::Algo_BooleanXor,
        OperationId::Algo_ReliefEngravingFromImage,
    };

    // ---- 编辑操作占位（待接入 GeometryEditService）----
    const OperationId editOps[] = {
        OperationId::Edit_Trim,
        OperationId::Edit_Extend,
        OperationId::Edit_Align,
        OperationId::Edit_Cut,
        OperationId::Edit_Paste,
        // Edit_GroupToggle, 已在 CoreOperationRegistry 中实现
        // Edit_Ungroup,     已在 CoreOperationRegistry 中实现
        OperationId::Edit_MirrorH,
        OperationId::Edit_MirrorV,
    };

    // ---- 视图操作占位（待接入 ViewController）----
    const OperationId viewOps[] = {
        OperationId::View_ZoomSelection,
        OperationId::View_Pan,
        OperationId::View_Reset,
        OperationId::View_GridVisible,
        OperationId::View_SnapEnabled,
        OperationId::View_OrthoMode,
        OperationId::View_AngleSnap,
        OperationId::View_LayerManager,
        OperationId::View_NewLayer,
        OperationId::View_DeleteLayer,
        OperationId::View_SetDisplayUnit,
    };

    auto registerPlaceholders = [&reg](const OperationId* ops, size_t count, const char* category) {
        int registered = 0;
        for (size_t i = 0; i < count; ++i)
        {
            if (!reg.has(ops[i]))
            {
                reg.registerOperation(std::make_unique<LambdaOperation>(
                    ops[i], [opId = ops[i], category] {
                        SY_WARNF("[PendingOp] %s: OperationId=%d not yet implemented",
                            category, static_cast<int>(opId));
                    }));
                ++registered;
            }
        }
        if (registered > 0)
        {
            SY_INFOF("[Composition] Registered %d placeholder operations for %s", registered, category);
        }
        };

    registerPlaceholders(algoOps, std::size(algoOps), "Algorithm");
    registerPlaceholders(editOps, std::size(editOps), "Edit");
    registerPlaceholders(viewOps, std::size(viewOps), "View");
}

void ApplicationCompositionRoot::registerHelpOperations()
{
    if (!m_operationBus)
        return;

    auto& reg = m_operationBus->registry();
    QWidget* parentWidget = m_shellHost ? m_shellHost->mainWindow() : nullptr;

    // ---- Help: About ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_About, [parentWidget] {
            HelpDialogService::showAboutDialog(parentWidget);
        }));

    // ---- Help: Settings ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Settings, [parentWidget] {
            HelpDialogService::showSettingsDialog(parentWidget);
        }));

    // ---- Help: Documentation ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Docs, [parentWidget] {
            HelpDialogService::showDocumentationDialog(parentWidget);
        }));

    // ---- Help: Keyboard Shortcuts ----
    reg.registerOperation(std::make_unique<LambdaOperation>(
        OperationId::Help_Shortcut, [parentWidget] {
            HelpDialogService::showShortcutsDialog(parentWidget);
        }));
}

void ApplicationCompositionRoot::saveDocumentPersistenceRecord(const QString& filePath, int entityCount)
{
    if (!m_persistenceService || !m_persistenceService->documents())
        return;

    auto existing = m_persistenceService->documents()->loadByPath(filePath.toStdString());
    QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    DocumentRecord dr;
    dr.filePath = filePath.toStdString();
    dr.title = QFileInfo(filePath).fileName().toStdString();
    dr.format = QFileInfo(filePath).suffix().toUpper().toStdString();
    dr.entityCount = entityCount;
    dr.lastOpenedAt = now.toStdString();
    dr.createdAt = existing.id > 0 ? existing.createdAt : now.toStdString();

    m_persistenceService->documents()->save(dr);
}