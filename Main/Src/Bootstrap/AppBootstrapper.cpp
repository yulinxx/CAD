#include "AppBootstrapper.h"

#include <QDir>
#include <QString>

#include "Log/SyLogger.h"
#include "UI/UiWorkbench.h"
#include "UI/WorkbenchWindow.h"
#include "UI/UiFrameworkServices.h"

namespace
{
    // 构建框架服务集合，将应用根组件的服务注入到UI框架层
    UiFrameworkServices buildFrameworkServices(ApplicationCompositionRoot* root)
    {
        UiFrameworkServices services;

        if (!root)
            return services;

        services.stateCenter = root->stateCenter();

        // 错误报告回调：将错误信息输出到日志系统
        services.reportError = [](const QString& errorCode, const QString& message, const QString& context) {
            SY_ERRORF("[Error] code=%s message=%s context=%s",
                errorCode.toUtf8().constData(),
                message.toUtf8().constData(),
                context.toUtf8().constData());
            };

        // 性能记录回调：记录关键操作的耗时
        services.recordPerformance = [](const QString& scope, qint64 elapsedMs) {
            SY_DEBUGF("[perf] %s: %lld ms", scope.toUtf8().constData(), static_cast<long long>(elapsedMs));
            };

        return services;
    }
}

// 构建应用路径集合，从路径管理器获取各目录的标准路径
AppPaths MainApp::buildAppPaths(const std::string& appName)
{
    AppPaths paths;

    paths.appRootPath = AppPathManager::appRootDir().toStdWString();
    paths.configDir = AppPathManager::configDir().toStdWString();
    paths.resourcesDir = AppPathManager::resourcesDir().toStdWString();
    paths.pluginsDir = AppPathManager::pluginsDir().toStdWString();

    return paths;
}

// 构造函数：初始化应用引导器，保存应用路径和版本信息
AppBootstrapper::AppBootstrapper(const AppPaths& paths, const std::string& appName, const std::string& version)
    : m_paths(paths)
    , m_appName(appName)
    , m_version(version)
{
    SY_INFOF("[AppBootstrapper] Created: name=%s, version=%s", appName.c_str(), version.c_str());
}

// 析构函数：确保在销毁前执行关闭流程
AppBootstrapper::~AppBootstrapper()
{
    shutdown();
    m_compositionRoot.reset();
}

// 初始化应用组合根组件，创建并验证所有必需的服务
bool AppBootstrapper::initialize()
{
    SY_INFO("[AppBootstrapper] Initializing composition root");

    m_compositionRoot = std::make_unique<ApplicationCompositionRoot>();

    if (!m_compositionRoot)
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_create_failed message=Failed to create ApplicationCompositionRoot");
        return false;
    }

    if (!m_compositionRoot->stateCenter())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing state center");
        return false;
    }
    if (!m_compositionRoot->themeService())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing theme service");
        return false;
    }
    if (!m_compositionRoot->layoutService())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing layout service");
        return false;
    }
    if (!m_compositionRoot->shellHost())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing shell host");
        return false;
    }

    SY_INFO("[AppBootstrapper] ApplicationCompositionRoot initialized successfully");
    return true;
}

// 执行应用引导序列，组装UI服务并启动工作台
void AppBootstrapper::bootstrap()
{
    SY_INFO("[AppBootstrapper] Starting bootstrap sequence");

    if (!m_compositionRoot)
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.no_root message=bootstrap() called without composition root");
        return;
    }

    m_services.stateCenter = m_compositionRoot->stateCenter();
    m_services.themeService = m_compositionRoot->themeService();
    m_services.layoutService = m_compositionRoot->layoutService();
    m_services.interactionDispatcher = m_compositionRoot->interactionDispatcher();
    m_services.undoManager = m_compositionRoot->undoRedoManager();
    m_services.operationBus = m_compositionRoot->operationBus();
    m_services.layerManager = m_compositionRoot->layerManager();
    m_services.layerManagerBridge = m_compositionRoot->layerManagerBridge();
    m_services.layerEditService = m_compositionRoot->layerEditService();
    m_services.persistenceService = m_compositionRoot->persistenceService();
    m_services.layerPersistenceBridge = m_compositionRoot->layerPersistenceBridge();
    m_services.document2D = m_compositionRoot->document2D();
    m_services.importService = m_compositionRoot->importService();
    m_services.exportService = m_compositionRoot->exportService();
    m_services.sceneManager = m_compositionRoot->sceneManager();
    m_services.sceneEditService = m_compositionRoot->sceneEditService();

    const auto startWorkbenchId = m_startWorkbenchId.isEmpty() ? QStringLiteral("2D") : m_startWorkbenchId;
    SY_INFOF("[AppBootstrapper] Bootstrapping workbench: %s", startWorkbenchId.toUtf8().constData());

    if (m_compositionRoot->stateCenter())
        m_compositionRoot->stateCenter()->setCurrentWorkbenchId(startWorkbenchId);

#if BUILD_UI3D
    if (startWorkbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
        m_workbench = std::make_unique<Workbench3D>();
    else
#endif
        m_workbench = std::make_unique<Workbench2D>();

    if (!m_workbench->initialize(m_services))
    {
        SY_ERRORF("[AppBootstrapper] error code=bootstrap.workbench_init_failed message=Workbench '%s' initialization failed", startWorkbenchId.toUtf8().constData());
        return;
    }
    SY_INFOF("[AppBootstrapper] Workbench '%s' initialized successfully", startWorkbenchId.toUtf8().constData());

    auto* shell = m_compositionRoot->shellHost();
    shell->setFrameworkServices(buildFrameworkServices(m_compositionRoot.get()));
    shell->setUiServices(m_services);
    shell->setWorkbench(m_workbench.get());
    shell->initializeAndShow();

    SY_INFO("[AppBootstrapper] UI shell initialized and shown");
}

// 关闭应用，按顺序清理UI壳和工作台资源
void AppBootstrapper::shutdown()
{
    SY_INFO("[AppBootstrapper] Starting shutdown");

    if (m_compositionRoot)
    {
        if (auto* shell = m_compositionRoot->shellHost())
            shell->shutdown();
    }

    m_workbench.reset();

    SY_INFO("[AppBootstrapper] Shutdown complete");
}

// 获取启动工作台ID
QString AppBootstrapper::startWorkbenchId() const
{
    return m_startWorkbenchId;
}

// 获取应用组合根组件指针
ApplicationCompositionRoot* AppBootstrapper::compositionRoot()
{
    return m_compositionRoot.get();
}

// 获取应用路径集合的常量引用
const AppPaths& AppBootstrapper::appPaths() const
{
    return m_paths;
}

// 设置启动时使用的工作台ID
void AppBootstrapper::setStartWorkbenchId(const QString& workbenchId)
{
    m_startWorkbenchId = workbenchId;
}