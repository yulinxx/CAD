/**
 * @file ApplicationCompositionRoot.cpp
 * @brief 应用组合根实现
 *
 * 负责组装和初始化所有核心服务。
 */
#include "ApplicationCompositionRoot.h"
#include "MachiningDataBridgeService.h"
#include "ImportExportServiceSetup.h"
#include "OperationRegistrar.h"

#include <QCoreApplication>

#include "UI2D/Operation/OperationRegistry.h"
#include "UI2D/Operation/OperationRouting.h"

#include "UI/Services/UiLayoutService.h"
#include "UI/Services/UiServices.h"
#include "UI/Services/UiShellHost.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI/Workbench/UiWorkbench.h"
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

#include "Engine/Text/FontManager.h"
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

#include "Export/ExportService.h"
#include "Export/ExportDispatcher.h"

// 静态实例指针，由构造函数设置，析构函数清空
static ApplicationCompositionRoot* s_instance = nullptr;

// 极早期启动路径（组合根尚未创建）的 SettingsService 兜底实例。
// 组合根构造时会接管此实例，保证进程内只有一个 SettingsService。
static std::unique_ptr<SettingsService> s_settingsFallback;

// 应用组合根组件，负责创建和组装所有核心服务
// 作为依赖注入的中心点，管理UI层和命令系统的生命周期
ApplicationCompositionRoot::~ApplicationCompositionRoot()
{
    // 清理静态实例指针
    s_instance = nullptr;

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

    // FontManager 的 FT_Library 同为 Meyers 单例持有，静态析构顺序不可控。
    // 在此显式释放，析构函数兜底保证恰好释放一次（见 FontManager::shutdown）。
    // GmcKernel 只持 atomic 标志、无可释放资源，无需在此收口。
    Eg::FontManager::instance().shutdown();
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

Eg::MachineFrameConfig ApplicationCompositionRoot::machineFrameConfig() const
{
    return m_deviceHost ? m_deviceHost->machineFrameConfig() : Eg::MachineFrameConfig{};
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
    , m_shellHost(std::make_unique<UiShellHost>())
    , m_operationBus(std::make_unique<OperationBus>())
    , m_sceneManager(std::make_unique<Eg::SceneManager>())
    , m_sceneManager3D(std::make_unique<Eg::SceneManager3D>())
    , m_machiningDataBridge(std::make_unique<MachiningDataBridgeService>(m_sceneManager.get(), m_sceneManager3D.get()))
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
    // 设置静态实例指针
    s_instance = this;

    // 接管持久化服务的唯一所有权（AppInitializer 创建，此处移交，不做惰性获取）。
    // 必须在任何持有 PersistenceService* 裸指针的消费方创建之前完成。
    m_persistenceService = AppInitializer::takePersistenceService();

    // 设置服务由组合根持有：它有自己的数据库连接与生命周期，跟组合根一起析构。
    // 以前是 getSettingsService() 里的函数内 static，既不随组合根销毁，
    // 也让 6 处 UI 通过「组合根的静态方法」反向依赖组合根 —— 组合根是装配入口，
    // 不该同时充当全局注册表。
    // 极早期启动路径若已创建兜底实例则直接接管，保证进程内只有一个实例。
    if (s_settingsFallback)
    {
        m_settingsService = std::move(s_settingsFallback);
    }
    else
    {
        m_settingsService = std::make_unique<SettingsService>(nullptr);
        m_settingsService->init();
    }

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

    m_operationRegistrar = std::make_unique<OperationRegistrar>(*this, algorithmRunner());
    m_operationRegistrar->registerAll();

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
    uiServices.persistenceService = persistenceService();
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
    // 读取器/写入器注册、服务注入与进度信号接线统一在 ImportExportServiceSetup
    ImportExportServiceSetup setup(m_stateCenter.get(),
        m_importService.get(),
        m_importDispatcher.get(),
        m_exportService.get(),
        m_exportDispatcher.get(),
        m_sceneManager.get(),
        m_sceneManager3D.get(),
        m_sceneEditService.get(),
        m_layerManager.get(),
        m_persistenceService.get(),
        m_captureService.get());
    setup.setup(uiServices);
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
    // 单一所有权：组合根构造时已从 AppInitializer 接管，这里只借用，不做惰性获取。
    // 析构顺序由成员声明顺序保证（m_persistenceService 声明在最前，析构在最后）。
    return m_persistenceService.get();
}

SettingsService* ApplicationCompositionRoot::getSettingsService()
{
    // 组合根已存在：返回它持有的实例（生命周期跟组合根走）
    if (s_instance && s_instance->m_settingsService)
    {
        return s_instance->m_settingsService.get();
    }

    // 组合根还没建（极早期启动路径）：临时持有一份，组合根构造时接管此实例
    if (!s_settingsFallback)
    {
        s_settingsFallback = std::make_unique<SettingsService>(nullptr);
        s_settingsFallback->init();
    }
    return s_settingsFallback.get();
}

ApplicationCompositionRoot* ApplicationCompositionRoot::instance()
{
    return s_instance;
}

void ApplicationCompositionRoot::saveCurrentWorkbenchSettings()
{
    // 获取 ShellHost 并尝试保存当前工作台的设置
    auto* self = instance();
    if (!self)
    {
        return;
    }

    auto* shellHost = self->shellHost();
    if (!shellHost)
    {
        return;
    }

    // 尝试获取当前工作台
    auto* workbench = shellHost->currentWorkbench();
    if (workbench)
    {
        workbench->saveCurrentSettings();
    }
}