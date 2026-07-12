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
        services.commandDispatcher = root->commandDispatcher();

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

    SY_DEBUG("[AppBootstrapper] Composition root created");

    // 验证状态中心服务是否可用
    if (!m_compositionRoot->stateCenter())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing state center");
        return false;
    }
    SY_DEBUG("[AppBootstrapper] State center available");

    // 验证主题服务是否可用
    if (!m_compositionRoot->themeService())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing theme service");
        return false;
    }
    SY_DEBUG("[AppBootstrapper] Theme service available");

    // 验证布局服务是否可用
    if (!m_compositionRoot->layoutService())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing layout service");
        return false;
    }
    SY_DEBUG("[AppBootstrapper] Layout service available");

    // 验证命令分发器是否可用
    if (!m_compositionRoot->commandDispatcher())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing command dispatcher");
        return false;
    }
    SY_DEBUG("[AppBootstrapper] Command dispatcher available");

    // 验证壳宿主服务是否可用
    if (!m_compositionRoot->shellHost())
    {
        SY_ERROR("[AppBootstrapper] error code=bootstrap.root_missing_service message=ApplicationCompositionRoot missing shell host");
        return false;
    }
    SY_DEBUG("[AppBootstrapper] Shell host available");

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

    // 组装UI服务集合，从组合根获取各服务引用
    SY_DEBUG("[AppBootstrapper] Assembling UI services");
    m_services = { m_compositionRoot->stateCenter(), m_compositionRoot->themeService(),
        m_compositionRoot->layoutService(), m_compositionRoot->commandDispatcher() };
    m_services.interactionDispatcher = m_compositionRoot->interactionDispatcher();
    m_services.undoStack = m_compositionRoot->undoStack();
    SY_DEBUG("[AppBootstrapper] UI services assembled");

    // 确定启动工作台ID，默认使用2D工作台
    const auto startWorkbenchId = m_startWorkbenchId.isEmpty() ? QStringLiteral("2D") : m_startWorkbenchId;
    SY_INFOF("[AppBootstrapper] Bootstrapping workbench: %s", startWorkbenchId.toUtf8().constData());

    // 在状态中心中设置当前工作台ID
    if (m_compositionRoot->stateCenter())
    {
        m_compositionRoot->stateCenter()->setCurrentWorkbenchId(startWorkbenchId);
        SY_DEBUGF("[AppBootstrapper] Set current workbench in state center: %s", startWorkbenchId.toUtf8().constData());
    }

    // 根据工作台ID创建对应的工作台实例
    SY_DEBUGF("[AppBootstrapper] Creating workbench instance: %s", startWorkbenchId.toUtf8().constData());
    if (startWorkbenchId.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
        m_workbench = std::make_unique<Workbench3D>();
    else
        m_workbench = std::make_unique<Workbench2D>();
    SY_DEBUGF("[AppBootstrapper] Workbench instance created: %s", startWorkbenchId.toUtf8().constData());

    // 初始化工作台
    SY_DEBUGF("[AppBootstrapper] Initializing workbench: %s", startWorkbenchId.toUtf8().constData());
    if (!m_workbench->initialize(m_services))
    {
        SY_ERRORF("[AppBootstrapper] error code=bootstrap.workbench_init_failed message=Workbench '%s' initialization failed", startWorkbenchId.toUtf8().constData());
        return;
    }
    SY_INFOF("[AppBootstrapper] Workbench '%s' initialized successfully", startWorkbenchId.toUtf8().constData());

    // 设置UI壳并启动界面
    SY_DEBUG("[AppBootstrapper] Setting up UI shell");
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
        SY_DEBUG("[AppBootstrapper] Shutting down UI shell");
        if (auto* shell = m_compositionRoot->shellHost())
            shell->shutdown();
        SY_DEBUG("[AppBootstrapper] UI shell shutdown complete");
    }

    SY_DEBUG("[AppBootstrapper] Cleaning up workbench");
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