#include "ApplicationCompositionRoot.h"
#include "FileOperationRegistry.h"
#include "CoreOperationRegistry.h"
#include "PendingOperationRegistry.h"
#include "DocumentPersistenceHelper.h"

#include "Operation/EditOperations.h"
#include "UI2D/Operation/OperationRegistry.h"

#include "UI/Services/UiLayoutService.h"
#include "UI/Services/UiServices.h"
#include "UI/Services/UiShellHost.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/UiThemeService.h"
#include "UI/Services/HelpDialogService.h"

#include "UI2D/Edit/QtLayerManagerBridge.h"

#include "Common/AppInitializer.h"

#include "Persistence/LayerPersistenceBridge.h"
#include "Persistence/PersistenceService.h"

#include "Log/SyLogger.h"

#include "UI/Settings/SettingsService.h"

SettingsService* getSettingsService()
{
    static std::unique_ptr<SettingsService> s;
    if (!s)
    {
        s = std::make_unique<SettingsService>(nullptr);
        s->init();
    }
    return s.get();
}

#include "UI/Services/FileDialogService.h"
#include "UI/Services/RecentFileService.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "UI/Services/SelectionService.h"
#include "UI/Services/ISelectionService.h"

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

#include <QWidget>

// 应用组合根组件，负责创建和组装所有核心服务
// 作为依赖注入的中心点，管理UI层和命令系统的生命周期
ApplicationCompositionRoot::~ApplicationCompositionRoot() = default;

ISelectionService* ApplicationCompositionRoot::selectionService()
{
    return m_selectionService.get();
}

// 应用组合根构造函数
// 职责：创建所有核心服务实例，完成依赖注入和信号连接
// 装配顺序：UI 基础服务 → 导入导出 → 对话框服务 → 脏状态同步 → 操作注册
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
    , m_selectionService(std::make_unique<SelectionService>(m_sceneManager.get()))
    , m_document2D(std::make_unique<SceneDocument2D>(m_sceneEditService.get()))
    , m_layerManager(std::make_unique<LayerManager>(m_sceneManager.get()))
    , m_layerManagerBridge(std::make_unique<QtLayerManagerBridge>(nullptr))
    , m_layerEditService(
          std::make_unique<LayerEditService>(m_layerManager.get(), m_undoRedoManager.get(), m_sceneManager.get()))
    , m_fileIOManager(std::make_unique<Fio::FileIOManager>())
    , m_importService(std::make_unique<ImportService>())
    , m_importDispatcher(std::make_unique<ImportDispatcher>())
    , m_exportService(std::make_unique<ExportService>())
    , m_exportDispatcher(std::make_unique<ExportDispatcher>())
{
    // 装配顺序：UI 服务 → 导入导出 → 对话框 → 脏状态 → 操作注册
    UiServices uiServices = assembleUiServices();
    setupImportExportServices(uiServices);
    m_shellHost->setUiServices(uiServices);
    setupDialogServices();
    setupDirtyStateSync();
    registerAllOperations();

    SY_INFO("[ApplicationCompositionRoot] initialized with module-based operation registration");
}

// ==================== 构造函数拆分子方法（P5 结构性优化） ====================

UiServices ApplicationCompositionRoot::assembleUiServices()
{
    // 创建最近文件服务
    m_recentFileService = std::make_unique<RecentFileService>(persistenceService());

    // 组装 UI 服务集合
    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();
    uiServices.selectionService = m_selectionService.get();
    uiServices.layerManager = m_layerManager.get();
    uiServices.layerManagerBridge = m_layerManagerBridge.get();
    uiServices.layerEditService = m_layerEditService.get();
    uiServices.persistenceService = persistenceService();
    uiServices.recentFileService = m_recentFileService.get();

    // LayerManagerBridge 注册为 LayerManager 观察者
    m_layerManager->addObserver(m_layerManagerBridge.get());

    // 创建图层持久化桥接器（运行态图层变更同步写入数据库）
    if (persistenceService() && persistenceService()->isOpen() && persistenceService()->layers())
    {
        m_layerPersistenceBridge =
            std::make_unique<LayerPersistenceBridge>(m_layerManager.get(), persistenceService()->layers());

        m_layerPersistenceBridge->attach();
        uiServices.layerPersistenceBridge = m_layerPersistenceBridge.get();
        SY_INFO("[ApplicationCompositionRoot] LayerPersistenceBridge attached");
    }

    // LayerManager 注入 SceneEditService，添加图元时自动分配图层
    m_sceneEditService->setLayerManager(m_layerManager.get());

    // 配置 ShellHost 核心依赖
    m_shellHost->setStateCenter(m_stateCenter.get());
    m_shellHost->setThemeService(m_themeService.get());
    m_shellHost->setOperationBus(m_operationBus.get());

    return uiServices;
}

void ApplicationCompositionRoot::setupImportExportServices(UiServices& uiServices)
{
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
    m_importService->setBusyStateCallback([this](bool busy) {
        if (m_stateCenter)
        {
            m_stateCenter->setBusy(busy);
        }
    });

    m_importService->setStatusPromptCallback([this](const QString& prompt) {
        if (m_stateCenter)
        {
            m_stateCenter->setMetadata({ { QStringLiteral("statusPrompt"), prompt } });
        }
    });

    m_importService->setDocumentPersistenceCallback([this](const QString& filePath, int entityCount) {
        DocumentPersistenceHelper::recordImport(m_persistenceService, filePath, entityCount);
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
    m_exportService->setBusyStateCallback([this](bool busy) {
        if (m_stateCenter)
        {
            m_stateCenter->setBusy(busy);
        }
    });

    m_exportService->setStatusPromptCallback([this](const QString& prompt) {
        if (m_stateCenter)
        {
            m_stateCenter->setMetadata({ { QStringLiteral("statusPrompt"), prompt } });
        }
    });

    // 注入到 UI 服务集合
    uiServices.importService = m_importService.get();
    uiServices.exportService = m_exportService.get();

    // 导入进度 → 状态中心
    QObject::connect(m_importService.get(), &ImportService::importStarted, m_stateCenter.get(), [this](const QString&) {
        if (m_stateCenter)
        {
            m_stateCenter->setBusy(true);
        }
    });

    QObject::connect(
        m_importService.get(), &ImportService::importFinished, m_stateCenter.get(), [this](const ImportResult& result) {
            if (!m_stateCenter)
            {
                return;
            }
            m_stateCenter->setBusy(false);
            QVariantMap meta = m_stateCenter->metadata();
            meta["statusPrompt"] = result.success ? QString("Import complete: %1 entities").arg(result.entityCount)
                                                  : QString("Import failed: %1").arg(result.message);
            meta["notificationType"] = result.success ? "info" : "error";
            m_stateCenter->setMetadata(meta);
        });

    // 导出进度 → 状态中心
    QObject::connect(m_exportService.get(), &ExportService::exportStarted, m_stateCenter.get(), [this](const QString&) {
        if (m_stateCenter)
        {
            m_stateCenter->setBusy(true);
        }
    });

    QObject::connect(
        m_exportService.get(), &ExportService::exportFinished, m_stateCenter.get(), [this](const ExportResult& result) {
            if (!m_stateCenter)
            {
                return;
            }

            m_stateCenter->setBusy(false);

            QVariantMap meta = m_stateCenter->metadata();
            meta["statusPrompt"] = result.success
                ? QString("Export complete: %1 entities").arg(result.exportedEntityCount)
                : QString("Export failed: %1").arg(result.message);
            meta["notificationType"] = result.success ? "info" : "error";

            m_stateCenter->setMetadata(meta);
        });
}

void ApplicationCompositionRoot::setupDialogServices()
{
    m_fileDialogService = std::make_unique<FileDialogService>();
    m_helpDialogService = std::make_unique<HelpDialogService>();
}

void ApplicationCompositionRoot::setupDirtyStateSync()
{
    // 场景变更 → 标记为未保存
    if (m_sceneManager)
    {
        m_sceneManager->setSceneChangedCallback(
            [](void* ctx) {
                auto* self = static_cast<ApplicationCompositionRoot*>(ctx);
                if (self->m_stateCenter && !self->m_stateCenter->dirty())
                {
                    self->m_stateCenter->setDirty(true);
                }
            },
            this);
    }
}

void ApplicationCompositionRoot::registerAllOperations()
{
    // OperationBus 设为活动实例
    OperationBus::setActiveInstance(m_operationBus.get());

    SY_INFO("[ApplicationCompositionRoot] registering module operations on OperationBus");

    // 旧 UI2D 框架的编辑操作（优先注册，first-wins 优先于 CoreOperationRegistry）
    {
        auto& registry = m_operationBus->registry();
        OperationEdit::registerAll(registry);
        OperationAlgo::registerAll(registry);
    }

    // 核心操作（撤销/重做/删除/圆角/倒角/帮助 + 编辑操作）
    CoreOperationRegistry coreOps(m_operationBus.get(),
        m_sceneEditService.get(),
        m_undoRedoManager.get(),
        m_shellHost ? m_shellHost->mainWindow() : nullptr);

    coreOps.registerAll();

    // 文件操作（导入/导出/打开/保存）
    FileOperationConfig fileConfig;
    fileConfig.bus = m_operationBus.get();
    fileConfig.sceneManager = m_sceneManager.get();
    fileConfig.importService = m_importService.get();
    fileConfig.exportService = m_exportService.get();
    fileConfig.recentFiles = m_recentFileService.get();
    fileConfig.helpDialog = m_helpDialogService.get();
    fileConfig.stateCenter = m_stateCenter.get();
    fileConfig.layerPersistence = m_layerPersistenceBridge.get();
    fileConfig.persistence = persistenceService();
    fileConfig.parentWidget = m_shellHost ? m_shellHost->mainWindow() : nullptr;

    m_fileOperationRegistry = std::make_unique<FileOperationRegistry>(fileConfig);
    m_fileOperationRegistry->registerAll();

    // 占位操作（视图缩放等）由 PendingOperationRegistry 统一管理
    PendingOperationRegistry pendingOps(m_operationBus.get());
    pendingOps.registerAll();
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
    {
        m_persistenceService = AppInitializer::persistenceService();
    }

    return m_persistenceService;
}

SettingsService* ApplicationCompositionRoot::getSettingsService()
{
    static std::unique_ptr<SettingsService> s;
    if (!s)
    {
        s = std::make_unique<SettingsService>(nullptr);
        s->init();
    }
    return s.get();
}
