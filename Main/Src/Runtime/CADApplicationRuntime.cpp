#include "CADApplicationRuntime.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QObject>
#include <QPixmap>
#include <QSettings>
#include <QSplashScreen>
#include <QThread>
#include <QCoreApplication>

#include <cstdio>

#include "Log/SyLogger.h"
#include "VersionInfo.h"
#include "Common/AppInitializer.h"
#include "Common/CrashHandlerBootstrap.h"
#include "Composition/ApplicationCompositionRoot.h"
#include "LicensingBootstrap.h"
#include "UI/ClientConfig/UiBrandManager.h"
#include "UI/ClientConfig/UiClientPluginHost.h"
#include "UI/ClientConfig/UiConfigSelfCheck.h"

#include "UI/Settings/SettingsService.h"

// 接收已创建的 QApplication（须由调用方在 buildAppPaths 之前创建），并设置应用基本信息
CADApplicationRuntime::CADApplicationRuntime(std::unique_ptr<QApplication> app, const AppPaths& appPaths)
    : m_app(std::move(app))
    , m_appPaths(appPaths)
{
    const auto& brand = UiBrandManager::instance();
    m_app->setApplicationName(QCoreApplication::applicationName());
    m_app->setApplicationVersion(QCoreApplication::applicationVersion());

    // 检测应用名称是否正确配置
    if (QCoreApplication::applicationName().isEmpty())
    {
        SY_WARN("[CADApplicationRuntime] Application name is empty! "
                "Check CMake configuration: APP_NAME may not be properly propagated to templates.");
    }
    // organizationName/Domain/窗口图标/默认主题 已在 CADApplicationEntry 中应用品牌层
    SY_INFOF("[CADApplicationRuntime] Brand identity: product='%s' company='%s' client='%s'",
        qPrintable(brand.productName()),
        qPrintable(brand.companyName()),
        qPrintable(brand.clientId()));

    // 设置当前工作目录到应用根目录
    if (!m_appPaths.appRootPath.empty())
    {
        QDir::setCurrent(QString::fromStdWString(m_appPaths.appRootPath.wstring()));
    }
}

// 按顺序关闭引导器和应用初始化器
CADApplicationRuntime::~CADApplicationRuntime()
{
    if (m_bootstrapper)
    {
        m_bootstrapper->shutdown();
        m_bootstrapper.reset();
    }

    CrashHandlerBootstrap::shutdown();

    m_app.reset();
    AppInitializer::shutdown();

    SY_INFO("[CADApplicationRuntime] Application shutdown complete");
}

// 运行应用主循环，执行初始化、许可证检查、引导和事件循环
int CADApplicationRuntime::run()
{
    const auto& brand = UiBrandManager::instance();

    QElapsedTimer splashTimer;
    splashTimer.start();
    std::unique_ptr<QSplashScreen> splash;
    const QString splashPath = brand.assetPath(brand.config().splash);
    if (!splashPath.isEmpty())
    {
        const QPixmap splashPixmap(splashPath);
        if (!splashPixmap.isNull())
        {
            splash = std::make_unique<QSplashScreen>(splashPixmap);
            splash->show();
            m_app->processEvents();
        }
    }

    // 初始化应用基础服务
    AppInitializer::initialize();

    // 客户插件必须在工作台/菜单/布局构建之前加载，面板注册表才能拿到插件工厂
    UiClientPluginHost::instance().loadForClient(brand.clientId());

    // 初始化崩溃处理
    //
    // SANYI_DISABLE_CRASH_HANDLER=1 时跳过：Breakpad 在 macOS 上抢的是 Mach 异常端口，
    // 优先级高于 AddressSanitizer 的信号处理，装上它 ASan 就永远打不出 free 栈/use 栈，
    // 只剩一个 minidump。排查内存问题（ASan / Valgrind）时必须让它让位。
    if (qEnvironmentVariableIntValue("SANYI_DISABLE_CRASH_HANDLER") != 0)
    {
        SY_DEBUG("[CADApplicationRuntime] CrashHandler disabled");
    }
    else if (!CrashHandlerBootstrap::initialize(
                 brand.productName().toStdString(), QString::fromStdString(MainApp::appVersion()).toStdString()))
    {
        SY_WARN("[CADApplicationRuntime] CrashHandler initialization failed, continuing without crash capture");
    }

    // 授权闸门（试用期/许可 UI 流程）：独立单元，见 LicensingBootstrap
    const int licensingExit = LicensingBootstrap::runGate(QString::fromStdWString(m_appPaths.configDir.wstring()));
    if (licensingExit != 0)
    {
        return licensingExit;
    }

    // 创建应用引导器并设置启动工作台
    m_bootstrapper = std::make_unique<AppBootstrapper>(
        m_appPaths, brand.productName().toStdString(), QCoreApplication::applicationVersion().toStdString());
    m_bootstrapper->setStartWorkbenchId(m_startWorkbenchId);

    // 初始化引导器
    // SY_DEBUG("[CADApplicationRuntime] Initializing bootstrapper");
    if (!m_bootstrapper->initialize())
    {
        SY_ERROR("[CADApplicationRuntime] error code=app.bootstrap_init_failed message=AppBootstrapper initialization "
                 "failed");
        return -2;
    }

    // 执行引导序列
    m_bootstrapper->bootstrap();

    // 验证引导结果
    if (!m_bootstrapper->compositionRoot())
    {
        SY_ERROR("[CADApplicationRuntime] error code=app.bootstrap_no_root message=Bootstrap completed without a valid "
                 "composition root");
        return -2;
    }

    // 配置可信性自检：CMake 开关 / JSON 配置 / License 授权 三侧交叉核对。
    // 必须放在 bootstrap() 之后 —— 客户配置是首次访问 UiConfigurationManager::shared()
    // 时才加载的，而那发生在菜单/工作台构建期间。
    UiConfigSelfCheck::runAndLogForCurrentClient();

    if (splash)
    {
        const int remaining = brand.config().splashDurationMs - static_cast<int>(splashTimer.elapsed());
        if (remaining > 0)
        {
            QThread::msleep(static_cast<unsigned long>(remaining));
        }
        splash->close();
        splash.reset();
    }

    // 退出时自动保存 common 设置（字体/主题/语言）到 SQLite 数据库
    // 以及当前工作台的 2D/3D 域设置（兜底防崩溃丢失）
    QObject::connect(m_app.get(), &QCoreApplication::aboutToQuit, []() {
        if (auto* svc = ApplicationCompositionRoot::getSettingsService())
        {
            svc->saveCurrentCommonSettings();
        }
        ApplicationCompositionRoot::saveCurrentWorkbenchSettings();
    });

    const int exitCode = m_app->exec();
    SY_INFOF("[CADApplicationRuntime] Application exited with code %d", exitCode);
    return exitCode;
}

void CADApplicationRuntime::setStartWorkbenchId(const QString& workbenchId)
{
    m_startWorkbenchId = workbenchId;
}