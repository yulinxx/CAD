#include "ApplicationCompositionRoot.h"
#include "FileOperationRegistry.h"
#include "CoreOperationRegistry.h"
#include "PendingOperationRegistry.h"
#include "LaserOperationRegistry.h"
#include "DocumentPersistenceHelper.h"


#include "UI2D/Operation/OperationRegistry.h"
#include "UI2D/Operation/OperationRouting.h"

#include "UI/Services/UiLayoutService.h"
#include "UI/Services/UIServices.h"
#include "UI/Services/UiShellHost.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/HelpDialogService.h"

#include "UI2D/Edit/QtLayerManagerBridge.h"

#include "../Hardware/DeviceHost.h"
#include "../Hardware/MachineProfile.h"
#include "../Hardware/ProcessingJobService.h"


#include "Common/AppInitializer.h"


// UndoRedoManager → OperationBus 桥接观察者
// 当 SceneEditService 等绕过 OperationBus::run() 直接推入 undo 命令时，
// 通过此观察者确保 OperationBus 发出 undoStateChanged 信号。
class UndoRedoObserverBridge : public IUndoRedoObserver
{
public:
    explicit UndoRedoObserverBridge(OperationBus* bus)
        : m_bus(bus)
    {
    }
    void onUndoStateChanged() override
    {
        if (m_bus)
        {
            emit m_bus->undoStateChanged();
        }
    }
    void onCanUndoChanged(bool) override {}
    void onCanRedoChanged(bool) override {}

private:
    OperationBus* m_bus{ nullptr };
};

#include "Persistence/LayerPersistenceBridge.h"
#include "Persistence/PersistenceService.h"

#include "Log/SyLogger.h"

#include "UI/Settings/SettingsService.h"

#include "UI/Services/FileDialogService.h"
#include "UI/Services/RecentFileService.h"

#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Edit/UndoRedoManager.h"
#include "Engine2D/Edit/SceneEditService.h"
#include "Engine2D/Interaction/LayerManager.h"

#include "UI/Services/SelectionService.h"
#include "UI/Services/ISelectionService.h"
#include "UI/Services/ViewportActionHub.h"
#include "UI/Service/ViewCaptureService.h"

#include "UI/Algorithm/AlgorithmApplicationService.h"
#include "UI2D/Operation/AlgorithmRunner.h"
#include "UI2D/AlgorithmTaskRegistration2D.h"
#include "UI2D/Manager/UnitManager.h"

#include "Import/ImportService.h"
#include "Import/ImportDispatcher.h"
#include "Import/Readers/DxfImportReader.h"
#include "Import/Readers/SvgImportReader.h"
#include "Import/Readers/PdfImportReader.h"
#include "Import/Readers/StepImportReader.h"
#include "Import/Readers/ObjImportReader.h"
#include "Import/Readers/StlImportReader.h"
#include "Import/Readers/PltImportReader.h"
#include "Import/Readers/AiImportReader.h"
#include "Import/Readers/NativeImportReader.h"
#include "Import/Readers/UgImportReader.h"

#include "Export/ExportService.h"
#include "Export/ExportDispatcher.h"
#include "Export/Writers/DxfExportWriter.h"
#include "Export/Writers/SvgExportWriter.h"
#include "Export/Writers/PdfExportWriter.h"
#include "Export/Writers/BmpExportWriter.h"
#include "Export/Writers/PngExportWriter.h"
#include "Export/Writers/ObjExportWriter.h"
#include "Export/Writers/StepExportWriter.h"
#include "Export/Writers/NativeExportWriter.h"

#include <QMessageBox>
#include <QWidget>

// 应用组合根组件，负责创建和组装所有核心服务
// 作为依赖注入的中心点，管理UI层和命令系统的生命周期
ApplicationCompositionRoot::~ApplicationCompositionRoot()
{
    // 加工服务先销毁：它的析构会把还在跑的作业 abort 掉，
    // 而 abort 需要设备还活着。反过来先停设备，作业就无处可停了。
    m_processingJobService.reset();

    // 硬件必须最先停：定时器停掉、设备 close（关光 + 输出置安全态）之后，
    // 其他服务才可以安全销毁。反过来的话，tick 里可能访问到已销毁的对象。
    if (m_deviceHost)
    {
        m_deviceHost->stop();
    }


    // 进程退出时，FillGeometryUpdater（Meyers 单例）的析构晚于本组合根，
    // 若不在 SceneEditService 仍存活时解绑，其析构会访问已销毁对象导致崩溃。
    Eg::FillGeometryUpdater::instance().detach();
}

DeviceHost* ApplicationCompositionRoot::deviceHost()
{
    return m_deviceHost.get();
}

ProcessingJobService* ApplicationCompositionRoot::processingJobService()
{
    return m_processingJobService.get();
}


bool ApplicationCompositionRoot::startHardware(const QString& configDir, QString& warningOut)
{
    QString loadWarning;

    const MachineProfile profile = MachineProfileLoader::loadOrFallback(configDir, loadWarning);

    QString startError;
    if (!m_deviceHost->start(profile, startError))
    {
        warningOut = startError;
        return false;
    }

    // 档案层的提示（例如「没有档案，已用模拟设备」）要继续往上传：
    // 设备确实起来了，但用户必须知道起来的是模拟设备而不是真机
    warningOut = loadWarning;
    return true;
}


ISelectionService* ApplicationCompositionRoot::selectionService()
{
    return m_selectionService.get();
}

// 应用组合根构造函数
// 职责：创建所有核心服务实例，完成依赖注入和信号连接
// 装配顺序：UI 基础服务 → 导入导出 → 对话框服务 → 脏状态同步 → 操作注册
ApplicationCompositionRoot::ApplicationCompositionRoot()
    : m_stateCenter(std::make_unique<UiStateCenter>())
    , m_layoutService(std::make_unique<DefaultUiLayoutService>())
    , m_interactionDispatcher(std::make_unique<DefaultInteractionDispatcher>())
    , m_shellHost(std::make_unique<UiShellHost>())
    , m_operationBus(std::make_unique<OperationBus>())
    , m_sceneManager(std::make_unique<Eg::SceneManager>())
    , m_sceneManager3D(std::make_unique<Eg::SceneManager3D>())
    , m_undoRedoManager(std::make_unique<UndoRedoManager>(m_sceneManager.get()))
    , m_sceneEditService(std::make_unique<SceneEditService>(m_sceneManager.get(), m_undoRedoManager.get()))
    , m_clipboard(std::make_unique<Eg::EntityClipboard>())
    , m_viewportActionHub(std::make_unique<ViewportActionHub>())
    , m_unitManager(std::make_unique<UnitManager>())
    , m_captureService(std::make_unique<Ui::ViewCaptureService>())
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
    m_uiServices = uiServices;
    m_shellHost->setUiServices(uiServices);
    setupDialogServices();
    setupDirtyStateSync();

    // 硬件宿主与加工服务必须在 registerAllOperations() **之前**创建。
    // 硬件真正启动（startHardware）发生在应用启动流程的后段，
    // 但注册进 OperationBus 的 lambda 捕获的是这两个对象的指针 ——
    // 对象要先存在，指针才稳定。启动前它们只是「设备未就绪」状态，
    // 加工命令会明确报「设备未启动」，而不是空指针。
    m_deviceHost = std::make_unique<DeviceHost>();
    m_processingJobService = std::make_unique<ProcessingJobService>(m_deviceHost.get());

    registerAllOperations();


    // 桥接 UndoRedoManager 观察者 → OperationBus::undoStateChanged
    // 当 SceneEditService 等绕过 OperationBus::run() 直接推入 undo 命令时，
    // 通过此观察者确保 OperationBus 发出信号，使 TopToolBar 等刷新按钮状态。
    m_undoRedoObserver = std::make_unique<UndoRedoObserverBridge>(m_operationBus.get());
    m_undoRedoManager->addObserver(m_undoRedoObserver.get());

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
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();
    uiServices.sceneEditService = m_sceneEditService.get();
    // 撤销/重做管理器必须填进 UiServices：Workbench2D::createToolbars 会把
    // m_services.undoManager **按值捕进** setUndoRedoProvider 的闭包，而
    // CommandActionHub::captureSnapshot 拿这个 provider 的结果**覆盖**
    // snapshot.canUndo / canRedo。这里漏填时闭包捕到的是 nullptr，
    // canUndo 恒为 false，edit.undo / edit.redo 的 RequiresUndo / RequiresRedo
    // 判定永远不通过 —— 菜单和工具栏的撤销/重做一直置灰，而置灰的 QAction
    // 连它的 Ctrl+Z / Ctrl+Y 都不会触发，表现就是「撤销完全没反应」。
    uiServices.undoManager = m_undoRedoManager.get();

    uiServices.selectionService = m_selectionService.get();
    uiServices.viewportActionHub = m_viewportActionHub.get();
    uiServices.unitManager = m_unitManager.get();
    uiServices.layerManager = m_layerManager.get();
    uiServices.layerManagerBridge = m_layerManagerBridge.get();
    uiServices.layerEditService = m_layerEditService.get();
    uiServices.recentFileService = m_recentFileService.get();
    uiServices.clipboard = m_clipboard.get();

    // LayerManagerBridge 注册为 LayerManager 观察者
    m_layerManager->addObserver(m_layerManagerBridge.get());

    // 创建图层持久化桥接器（运行态图层变更同步写入数据库）
    if (persistenceService() && persistenceService()->isOpen() && persistenceService()->layers())
    {
        m_layerPersistenceBridge =
            std::make_unique<LayerPersistenceBridge>(m_layerManager.get(), persistenceService()->layers());

        m_layerPersistenceBridge->attach();
        SY_DEBUG("[ApplicationCompositionRoot] LayerPersistenceBridge attached");
    }

    // LayerManager 注入 SceneEditService，添加图元时自动分配图层
    m_sceneEditService->setLayerManager(m_layerManager.get());

    // 填充几何更新协调器（单例）：图元移动/变换后增量重算色块填充（跟随位置）
    Eg::FillGeometryUpdater::instance().initialize(m_sceneManager.get(), m_layerManager.get(), m_sceneEditService.get());

    // 配置 ShellHost 核心依赖
    m_shellHost->setStateCenter(m_stateCenter.get());
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
    // 新增导入格式：Adobe Illustrator (AI) 与 Unigraphics/NX (UG, 经 IGES)
    m_importDispatcher->registerReader(std::make_unique<AiImportReader>());
    m_importDispatcher->registerReader(std::make_unique<NativeImportReader>());
    m_importDispatcher->registerReader(std::make_unique<UgImportReader>());
    SY_DEBUGF("[ImportDispatcher] Registered %lld reader(s): %s",
        static_cast<long long>(m_importDispatcher->supportedExtensions().size()),
        m_importDispatcher->supportedExtensions().join(QLatin1String(", ")).toUtf8().constData());

    // 配置导入服务
    m_importService->setDispatcher(m_importDispatcher.get());
    m_importService->setSceneManager(m_sceneManager.get());
    m_importService->setSceneManager3D(m_sceneManager3D.get());
    m_importService->setEditService(m_sceneEditService.get());
    // 注入图层管理器：DXF 等导入后按源图层表还原图层结构
    m_importService->setLayerManager(m_layerManager.get());
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
    m_exportDispatcher->registerWriter(std::make_unique<NativeExportWriter>());
    SY_DEBUGF("[ExportDispatcher] Registered %lld writer(s): %s",
        static_cast<long long>(m_exportDispatcher->supportedExtensions().size()),
        m_exportDispatcher->supportedExtensions().join(QLatin1String(", ")).toUtf8().constData());

    // 配置导出服务
    m_exportService->setDispatcher(m_exportDispatcher.get());
    m_exportService->setSceneManager(m_sceneManager.get());
    m_exportService->setSceneManager3D(m_sceneManager3D.get());
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

    // 注入到 UI 服务集合。导出服务不进 UiServices：唯一的消费者
    // FileOperationRegistry 走 FileOperationConfig::exportService 单独注入。
    uiServices.importService = m_importService.get();

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
    // OperationRouting 注入 OperationBus（替代全局单例）
    OperationRouting::setOperationBus(m_operationBus.get());

    SY_DEBUG("[ApplicationCompositionRoot] registering module operations on OperationBus");

    // 核心操作（撤销/重做/删除/圆角/倒角/选择/帮助 + 编辑操作 + 算法操作 + 视图操作）
    CoreOperationRegistry coreOps(m_operationBus.get(),
        m_sceneEditService.get(),
        m_undoRedoManager.get(),
        m_clipboard.get(),
        algorithmRunner(),
        m_viewportActionHub.get(),
        m_stateCenter.get(),
        m_layerEditService.get(),
        m_unitManager.get(),
        m_shellHost ? m_shellHost->mainWindow() : nullptr,
        m_captureService.get());

    coreOps.registerAll();

    // 文件操作（导入/导出/打开/保存）
    FileOperationConfig fileConfig;
    fileConfig.bus = m_operationBus.get();
    fileConfig.sceneManager = m_sceneManager.get();
    fileConfig.layerManager = m_layerManager.get();
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

    // 加工操作（开始/暂停/停止/急停）
    LaserOperationConfig laserConfig;
    laserConfig.bus = m_operationBus.get();
    laserConfig.deviceHost = m_deviceHost.get();
    laserConfig.jobService = m_processingJobService.get();
    laserConfig.sceneManager = m_sceneManager.get();
    laserConfig.layerManager = m_layerManager.get();
    laserConfig.errorReporter = [this](const QString& message) {
        // 加工失败必须让操作员看见：只写日志的话，
        // 现场表现是「按了开始加工什么都没发生」
        QMessageBox::warning(m_shellHost ? m_shellHost->mainWindow() : nullptr,
            QObject::tr("Processing"), message);
    };

    m_laserOperationRegistry = std::make_unique<LaserOperationRegistry>(laserConfig);
    m_laserOperationRegistry->registerAll();

    // 占位操作（视图缩放等）由 PendingOperationRegistry 统一管理
    PendingOperationRegistry pendingOps(m_operationBus.get());
    pendingOps.registerAll();
}


AlgorithmRunner* ApplicationCompositionRoot::algorithmRunner()
{
    if (!m_algorithmRunner)
    {
        m_algorithmService = std::make_unique<AlgorithmApplicationService>(nullptr);
        AlgorithmTaskRegistration2D::registerAll(*m_algorithmService);
        m_algorithmRunner = std::make_unique<AlgorithmRunner>(m_algorithmService.get(),
            m_sceneManager.get(),
            m_sceneEditService.get(),
            m_unitManager.get(),
            m_shellHost ? m_shellHost->mainWindow() : nullptr);
    }
    return m_algorithmRunner.get();
}

UiShellHost* ApplicationCompositionRoot::shellHost()
{
    return m_shellHost.get();
}

UiStateCenter* ApplicationCompositionRoot::stateCenter()
{
    return m_stateCenter.get();
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