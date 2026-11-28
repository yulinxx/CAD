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
#include <QTimer>

#include <functional>
#include <utility>

#include "UI2D/Operation/OperationRegistry.h"
#include "UI2D/Operation/OperationRouting.h"

#include "UI/Services/UiLayoutService.h"
#include "UI/Services/UiServices.h"
#include <QMainWindow>
#include "UI/Services/UiStateCenter.h"
#include "UI/Services/HelpDialogService.h"
#include "UI/Safety/EmergencyStopController.h"

#include "UI2D/Edit/QtLayerManagerBridge.h"

#include "../Hardware/DeviceHost.h"
#include "../Hardware/MachineProfile.h"
#include "../Hardware/ProcessingJobService.h"

#include "Common/AppInitializer.h"

#ifdef ENABLE_VISION
#include "../Vision/VisionHostBridges.h"
// Vision 的 C++ 门面不导出符号，宿主只能经 C ABI 初始化与注册桥接表。
#include "Vision/Core/VisionTypes.h"
#include "Vision/Service/VisionHostC.h"
#include "Vision/Service/VisionServiceC.h"
#endif

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
#include "Persistence/SharedDatabaseAdapter.h"

#include "Log/AprLogger.h"

#include "UI/Settings/SettingsService.h"

#include "UI/Services/FileDialogService.h"
#include "UI/Services/IRecentFileService.h"
#include "UI/Services/RecentFileService.h"

#include "Engine/FrameworkSingletonRegistry.h"
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
    // 清理静态实例指针，仅在是自身时清空：避免连续构造两个组合根时，
// 旧实例析构把新实例的 s_instance 误清。
    if (s_instance == this)
    {
        s_instance = nullptr;
    }

    // 显式撤销装配期建立的接线（观察者注册 / 桥接 attach / 单例回调 / 场景回调），
    // 不依赖成员声明顺序；teardownAssembled 幂等，此处与 build() 失败路径共用。
    teardownAssembled();

    // 加工服务先销毁：它的析构会把还在跑的作业 abort 掉，
    // 而 abort 需要设备还活着。反过来先停设备，作业就无处可停了。
    m_processingJobService.reset();

#ifdef ENABLE_VISION
    // 位姿采样读运动卡，必须在设备停止之前停掉；否则最后一拍可能踩到已关闭的设备。
    if (m_stagePoseProvider)
    {
        m_stagePoseProvider->stop();
    }

    // 先解除门面对宿主桥的引用，再销毁桥对象 —— 门面是进程级单例，
    // 它的注入槽不清空就会在下一个使用者手里变成悬空指针。
    VisHost::unregisterBridges();
    if (m_visionInitialized)
    {
        Vision_Shutdown();
    }

    m_workpieceTransformSink.reset();
    m_lightController.reset();
    m_galvoController.reset();
    m_stageController.reset();
    m_stagePoseProvider.reset();
#endif

    // 硬件必须最先停：定时器停掉、设备 close（关光 + 输出置安全态）之后，
    // 其他服务才可以安全销毁。反过来的话，tick 里可能访问到已销毁的对象。
    if (m_deviceHost)
    {
        m_deviceHost->stop();
    }

    // 框架级单例统一清理：FillGeometryUpdater / FontManager 等在首次创建时
    // 已向 FrameworkSingletonRegistry 注册清理回调，此处按注册逆序统一执行。
    // 组合根不再需要逐个手工 detach/shutdown，新增单例也不会漏清理，析构顺序
    // 由注册顺序保证（GmcKernel 只持 atomic 标志、无可释放资源，未注册）。
    Eg::FrameworkSingletonRegistry::clearAll();
}

DeviceHost* ApplicationCompositionRoot::deviceHost()
{
    return m_deviceHost.get();
}

ProcessingJobService* ApplicationCompositionRoot::processingJobService()
{
    return m_processingJobService.get();
}

EmergencyStopController* ApplicationCompositionRoot::emergencyStopController()
{
    return m_emergencyStopController.get();
}

ApplicationCompositionRoot::HardwareStartResult ApplicationCompositionRoot::startHardware(const QString& configDir)
{
    HardwareStartResult result;

    QString loadWarning;

    const MachineProfile profile = MachineProfileLoader::loadOrFallback(configDir, loadWarning);

    QString startError;
    if (!m_deviceHost->start(profile, startError))
    {
        result.message = startError;
        return result;  // started=false
    }

#ifdef ENABLE_VISION
    // 设备真的起来了才开始采样：启动失败时读运动卡只会得到一串无效点。
    if (m_stagePoseProvider)
    {
        m_stagePoseProvider->start();
    }
#endif

    // 档案层的提示（例如「没有档案，已用模拟设备」）要继续往上传：
    // 设备确实起来了，但用户必须知道起来的是模拟设备而不是真机
    result.started = true;
    result.message = loadWarning;
    return result;
}

Eg::MachineFrameConfig ApplicationCompositionRoot::machineFrameConfig() const
{
    return m_deviceHost ? m_deviceHost->machineFrameConfig() : Eg::MachineFrameConfig{};
}

#ifdef ENABLE_VISION
void ApplicationCompositionRoot::setupVisionHostBridges()
{
    // 门面是进程级单例：UI 侧适配层可能已经先初始化过（返回 AlreadyOpen），
    // 那种情况下保留已有配置，只补注入槽，不重复 initialize。
    const int32_t initCode = Vision_Initialize(nullptr);
    if (initCode == 0)
    {
        m_visionInitialized = true;
    }
    else if (initCode != static_cast<int32_t>(Vis::VisError::AlreadyOpen))
    {
        APR_WARNF("[ApplicationCompositionRoot] vision service init failed: code=%d", initCode);
        return;
    }

    m_stagePoseProvider = std::make_unique<VisHost::StagePoseProvider>(m_deviceHost.get());
    m_stageController = std::make_unique<VisHost::StageController>(m_deviceHost.get());
    m_galvoController = std::make_unique<VisHost::GalvoController>(m_deviceHost.get());
    m_lightController = std::make_unique<VisHost::LightController>(m_deviceHost.get());
    m_workpieceTransformSink =
        std::make_unique<VisHost::WorkpieceTransformSink>(m_sceneManager.get(), m_sceneEditService.get());

    if (!VisHost::registerBridges(m_stagePoseProvider.get(), m_stageController.get(),
                                  m_galvoController.get(), m_lightController.get(),
                                  m_workpieceTransformSink.get()))
    {
        APR_WARN("[ApplicationCompositionRoot] vision host bridges were rejected by the service");
    }
}
#endif


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
    // 构造函数只做成员初始化；装配逻辑集中在 build()，由 AppBootstrapper 显式调用。
}

// 显式装配阶段：接管持久化/设置服务、装配 UI 服务、接线、注册操作。
// 全部完成后才设置 s_instance，杜绝半初始化对象被 instance() 取用；
// 失败时 s_instance 保持为空，调用方应中止启动。
bool ApplicationCompositionRoot::build()
{
    // 幂等：装配已完成时直接返回。观察者、命令、桥接都不可重入，
    // 重复装配会重复注册（撤销观察者双发信号、命令二次覆盖）——
    // 这里显式拦下并留痕，而不是让调用方悄悄触发一遍副作用。
    if (m_buildCompleted)
    {
        APR_WARN("[ApplicationCompositionRoot] build() called again after completion; ignored");
        return true;
    }

    // 核心服务必须就绪：它们由构造函数成员初始化列表创建，为空意味着初始化失败。
    // 缺失时立即失败，绝不把一个缺关键依赖的根对象暴露成可用状态。
    if (!m_stateCenter || !m_operationBus || !m_mainWindow)
    {
        APR_ERROR("[ApplicationCompositionRoot] build failed: core service missing");
        // 当前失败发生在任何接线之前，teardownAssembled 为空操作；
        // 但后续若在装配中途新增失败点，必须同样先调用它再 return false，
        // 以保证已完成的接线（观察者/桥接/单例回调）被即时撤销。
        teardownAssembled();
        return false;
    }

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
    }

    // 设置侧改用业务库的唯一 SQLite 连接：此前 UICommon 自建第二条连接写同一个
    // cad_database.sqlite，WAL 下双写连接互相撞 SQLITE_BUSY（导入回写期间
    // RecentFile/Document 仓储重试全失败的 "database is locked" 即源于此）。
    // init 之前注入则直接生效；兜底实例已 init 则重建仓库切换过去。
    // PersistenceService 未就绪时 setExternalDatabase 无操作，保持旧行为（自建连接）。
    if (m_persistenceService && m_persistenceService->isOpen())
    {
        m_settingsDbAdapter = std::make_unique<SharedDatabaseAdapter>(m_persistenceService->database());
        m_settingsService->setExternalDatabase(m_settingsDbAdapter.get());
    }

    if (!m_settingsService->isInitialized())
    {
        m_settingsService->init();
    }

    // 装配顺序：UI 服务 → 导入导出 → 对话框 → 脏状态 → 操作注册
    UiServices uiServices = assembleUiServices();
    setupImportExportServices(uiServices);
    m_uiServices = uiServices;
    setupDialogServices();
    setupDirtyStateSync();

    // 硬件宿主与加工服务必须在 registerAllOperations() **之前**创建。
    // 硬件真正启动（startHardware）发生在应用启动流程的后段，
    // 但注册进 OperationBus 的 lambda 捕获的是这两个对象的指针 ——
    // 对象要先存在，指针才稳定。启动前它们只是「设备未就绪」状态，
    // 加工命令会明确报「设备未启动」，而不是空指针。
    m_deviceHost = std::make_unique<DeviceHost>();
    m_processingJobService = std::make_unique<ProcessingJobService>(m_deviceHost.get());

    // 急停状态机与设备宿主同生命周期，注册加工命令前先建立，保证命令与界面共享同一状态
    m_emergencyStopController = std::make_unique<EmergencyStopController>();
    m_emergencyStopController->attachDeviceHost(m_deviceHost.get());

    // M6：主窗口由 UiShell 持有，旧 WorkbenchWindow 的急停/设备面板绑定也随之移到
    // 客户自己的面板工厂；组合根只负责服务生命周期与 OperationBus 注册。

#ifdef ENABLE_VISION
    // 视觉门面在设备装配完成后立刻注入宿主能力：标定向导与飞拍在设备
    // 启动前就要能回答"有没有这个能力"，不能等到第一次调用才去补指针。
    setupVisionHostBridges();
#endif

    m_operationRegistrar = std::make_unique<OperationRegistrar>(*this, algorithmRunner());
    m_operationRegistrar->registerAll();

    // 桥接 UndoRedoManager 观察者 → OperationBus::undoStateChanged
    // 当 SceneEditService 等绕过 OperationBus::run() 直接推入 undo 命令时，
    // 通过此观察者确保 OperationBus 发出信号，使 TopToolBar 等刷新按钮状态。
    m_undoRedoObserver = std::make_unique<UndoRedoObserverBridge>(m_operationBus.get());
    m_undoRedoManager->addObserver(m_undoRedoObserver.get());

    APR_INFO("[ApplicationCompositionRoot] initialized with module-based operation registration");

    // 装配全部完成后才对外暴露：在此之前 instance() 返回 nullptr，
    // 杜绝外部拿到半初始化对象。失败分支（未来扩展）必须先调用 teardownAssembled()
    // 再 return false，且不设置 s_instance，调用方据此中止启动。
    s_instance = this;
    m_buildCompleted = true;
    return true;
}

// ==================== 构造函数拆分子方法（P5 结构性优化） ====================

UiServices ApplicationCompositionRoot::assembleUiServices()
{
    // 创建最近文件服务
    m_recentFileService = std::make_unique<RecentFileService>(persistenceService());

    // 算法应用服务（供工作台在算法运行期间禁用工具交互）
    // 提前创建，避免装配容器在注册器创建前就拿到空算法服务；
    // 后续 OperationRegistrar 仍会复用 m_algorithmService，不再重复创建。
    AlgorithmRunner* algorithmRunnerPtr = algorithmRunner();
    (void)algorithmRunnerPtr;

    // 组装 UI 服务集合
    UiServices uiServices;
    uiServices.uiState.stateCenter = m_stateCenter.get();
    uiServices.commands.operationBus = m_operationBus.get();
    uiServices.scene.document2D = m_document2D.get();
    uiServices.scene.sceneEditService = m_sceneEditService.get();
    // 撤销/重做管理器必须填进 UiServices：Workbench2D::createToolbars 会把
    // m_commands.undoManager **按值捕进** setUndoRedoProvider 的闭包，而
    // CommandActionHub::captureSnapshot 拿这个 provider 的结果**覆盖**
    // snapshot.canUndo / canRedo。这里漏填时闭包捕到的是 nullptr，
    // canUndo 恒为 false，edit.undo / edit.redo 的 RequiresUndo / RequiresRedo
    // 判定永远不通过 —— 菜单和工具栏的撤销/重做一直置灰，而置灰的 QAction
    // 连它的 Ctrl+Z / Ctrl+Y 都不会触发，表现就是「撤销完全没反应」。
    uiServices.commands.undoManager = m_undoRedoManager.get();

    uiServices.scene.selectionService = m_selectionService.get();
    uiServices.view.viewportActionHub = m_viewportActionHub.get();
    uiServices.view.unitManager = m_unitManager.get();
    uiServices.scene.layerManager = m_layerManager.get();
    uiServices.scene.layerManagerBridge = m_layerManagerBridge.get();
    uiServices.scene.layerEditService = m_layerEditService.get();
    uiServices.persistence.persistenceService = persistenceService();
    uiServices.persistence.recentFileService = m_recentFileService.get();
    uiServices.scene.clipboard = m_clipboard.get();
    uiServices.algorithm.algorithmApplicationService = m_algorithmService.get();
    // 设置服务显式注入各消费方（窗口/工作台/菜单分发器），消费方不再经
    // ApplicationCompositionRoot::getSettingsService() 静态回查组合根实例。
    uiServices.settings.settingsService = m_settingsService.get();

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

    // 注入延迟派发器：Engine 层不依赖 Qt，无法自己把重算推迟到当前调用栈之后，
    // 由宿主提供实现（见 FillGeometryUpdater.h 文件头说明）。
    // 用 singleShot(0)：编辑事务（拖动松开 / 变换命令）返回后、下一次事件循环迭代执行，
    // 此时场景已不在"提交中"，重算不会重入撤销栈或场景容器迭代。
    // 拖动只在 commitInteractive 收尾时触发一次回调（拖动过程中只标脏不动索引），
    // 因此这里不会退化成每帧全量重算；同一轮事件循环内的多次提交由协调器按 id 合并。
    Eg::FillGeometryUpdater::instance().setDeferredDispatcher([](std::function<void()> task) {
        QTimer::singleShot(0, QCoreApplication::instance(), std::move(task));
    });

    return uiServices;
}

void ApplicationCompositionRoot::setupImportExportServices(UiServices& uiServices)
{
    // 读取器/写入器注册、服务注入与进度信号接线统一在 ImportExportServiceSetup。
    // 用命名成员传参，避免 11 个位置参数中相邻同构类型写错顺序的静默 bug。
    ImportExportSetupContext ctx;
    ctx.stateCenter = m_stateCenter.get();
    ctx.importService = m_importService.get();
    ctx.importDispatcher = m_importDispatcher.get();
    ctx.exportService = m_exportService.get();
    ctx.exportDispatcher = m_exportDispatcher.get();
    ctx.sceneManager = m_sceneManager.get();
    ctx.sceneManager3D = m_sceneManager3D.get();
    ctx.editService = m_sceneEditService.get();
    ctx.layerManager = m_layerManager.get();
    ctx.persistence = m_persistenceService.get();
    ctx.captureService = m_captureService.get();
    ImportExportServiceSetup setup(ctx);
    setup.setup(uiServices);

    // 最近文件的服务级兜底回调：ImportContext 只有 FileOperationRegistry 的打开路径
    // 才带 recentFileAddCallback，普通导入（各格式菜单项、拖放）的 context 是空的，
    // 回写阶段会走这里 —— 否则导入过的文件永远进不了"最近文件"列表。
    if (m_importService && uiServices.persistence.recentFileService)
    {
        IRecentFileService* recentFiles = uiServices.persistence.recentFileService;
        m_importService->setRecentFileAddCallback(
            [recentFiles](const QString& path, const QString& workbenchId) {
                recentFiles->addRecentFile(path, workbenchId);
            });
    }
}

void ApplicationCompositionRoot::setupDialogServices()
{
    m_fileDialogService = std::make_unique<FileDialogService>();
    m_helpDialogService = std::make_unique<HelpDialogService>();
}

void ApplicationCompositionRoot::setupDirtyStateSync()
{
    // 2D 文档脏标记探针：状态中心据此重算 dirty（任一文档，供关闭/提示）与
    // activeDocDirty（当前工作台文档，供标题 "*"）。3D 探针由 Workbench3D 初始化时注册。
    if (m_stateCenter)
    {
        m_stateCenter->registerDirtyProbe(QStringLiteral("2D"), [this]() {
            return m_sceneManager && m_sceneManager->isDirty();
        });
    }

    // 场景变更 → 重新计算脏状态（探针回读引擎脏标记，此处不直接写值）
    if (m_sceneManager)
    {
        m_sceneManager->setSceneChangedCallback(
            [](void* ctx) {
                auto* self = static_cast<ApplicationCompositionRoot*>(ctx);
                if (self->m_stateCenter)
                {
                    self->m_stateCenter->refreshDirty();
                }
            },
            this);
    }
}

void ApplicationCompositionRoot::teardownAssembled()
{
    // 按 build() 装配的逆序撤销已完成的接线。每步做空值守卫，保证幂等：
    // build() 中途失败会调用一次，析构时再调用一次，不得因重复调用崩溃。
    // 只撤销「外部/回调侧效应」（观察者注册、桥接 attach、单例回调、场景回调），
    // 不释放任何 owned 服务——那是 unique_ptr 成员与析构的职责。

    // 撤销栈观察者（build 末尾注册，反向先撤）
    if (m_undoRedoManager && m_undoRedoObserver)
    {
        m_undoRedoManager->removeObserver(m_undoRedoObserver.get());
    }

    // 场景变更回调（setupDirtyStateSync 注册，捕获 this）—— 先清，避免销毁途中触发
    if (m_sceneManager)
    {
        m_sceneManager->setSceneChangedCallback({}, nullptr);
    }

    // 填充几何更新协调器是进程级单例：解绑场景并清空延迟派发器
    // （派发器 lambda 捕获宿主 QObject，不清空会在宿主销毁后悬空）
    Eg::FillGeometryUpdater::instance().detach();

    // 图层持久化桥接 detach
    if (m_layerPersistenceBridge)
    {
        m_layerPersistenceBridge->detach();
    }

    // LayerManagerBridge 观察者注销
    if (m_layerManager && m_layerManagerBridge)
    {
        m_layerManager->removeObserver(m_layerManagerBridge.get());
    }

    // SceneEditService 持有的 LayerManager 回指清空
    if (m_sceneEditService)
    {
        m_sceneEditService->setLayerManager(nullptr);
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
            m_layerEditService.get(),
            m_unitManager.get(),
            m_mainWindow);
    }
    return m_algorithmRunner.get();
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
    // 防御性断言：s_instance 非空但 m_buildCompleted 为假，意味着在装配完成前
    // 就对外暴露了半初始化对象（框架层违规）。s_instance 在 build() 末尾才设置，
    // 正常情况下两者同时为真；此检查兜底防止未来回归（如有人提前设置 s_instance）。
    // APR_ERROR 只记日志不崩溃，且 instance() 调用均在运行期，不会误报。
    if (s_instance != nullptr && !s_instance->m_buildCompleted)
    {
        APR_ERROR("[ApplicationCompositionRoot] instance() called before build() completed; half-initialized object exposed");
        return nullptr;   // 不再把半初始化对象暴露给外部
    }
    return s_instance;
}

SettingsService* ApplicationCompositionRoot::settingsService()
{
    return m_settingsService.get();
}

void ApplicationCompositionRoot::saveCurrentWorkbenchSettings()
{
    // M6：旧 WorkbenchWindow/UiWorkbench 已移到测试专用目录；窗口布局由 UiShell
    // 自己的 MainWindow::saveWindowState() 负责，这里保留接口以兼容退出流程。
}
