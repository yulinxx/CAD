#include "CADApplicationRuntime.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include <cstdio>

#include "Log/SyLogger.h"
#include "VersionInfo.h"
#include "Common/AppInitializer.h"
#include "Common/CrashHandlerBootstrap.h"
#include "License/LicenseDialog.h"
#include "License/LicenseDLL.h"
#include "Composition/ApplicationCompositionRoot.h"
#include "UI/Settings/SettingsService.h"

// 构造函数：接收已创建的 QApplication（须由调用方在 buildAppPaths 之前创建），并设置应用基本信息
CADApplicationRuntime::CADApplicationRuntime(std::unique_ptr<QApplication> app, const AppPaths& appPaths)
    : m_app(std::move(app))
    , m_appPaths(appPaths)
{
    m_app->setApplicationName(QString::fromStdString(MainApp::appName()));
    m_app->setApplicationVersion(QString::fromStdString(MainApp::appVersion()));
    m_app->setOrganizationName(QString::fromStdString(MainApp::organizationName()));
    m_app->setOrganizationDomain(QString::fromStdString(MainApp::organizationDomain()));
    m_app->setWindowIcon(QIcon(":/ui/common/Icons/Help/theme.svg"));

    // 设置当前工作目录到应用根目录
    if (!m_appPaths.appRootPath.empty())
    {
        QDir::setCurrent(QString::fromStdWString(m_appPaths.appRootPath));
    }
}

// 析构函数：按顺序关闭引导器和应用初始化器
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
    // 初始化应用基础服务
    AppInitializer::initialize();

    SY_INFOF("[CADApplicationRuntime] Initializing application: name=%s, version=%s",
        MainApp::appName().c_str(),
        MainApp::appVersion().c_str());

    // 初始化崩溃处理
    // SY_DEBUG("[CADApplicationRuntime] Initializing crash handler");
    if (!CrashHandlerBootstrap::initialize(MainApp::appName(), MainApp::appVersion()))
    {
        SY_WARN("[CADApplicationRuntime] CrashHandler initialization failed, continuing without crash capture");
    }
    // SY_INFO("[CADApplicationRuntime] Crash handler initialized");

    // [B1-P0 修复] 许可校验启动顺序修正：
    // 旧代码先调用 License_IsCheckEnabled()（恒为 false）再调用 License_ConfigInit()，
    // 导致整个许可校验块被跳过。现改为先初始化配置再检查开关。
    {
        LicenseConfig config{};
        License_ConfigInit(&config);
        const QString configDir = QString::fromStdWString(m_appPaths.configDir);
        const QByteArray configDirUtf8 = configDir.toUtf8();
        config.configDir = configDirUtf8.constData();

        // [B1-P0 修复] 通过编译期宏 SANYI_ENABLE_LICENSE 显式启用许可校验。
        // 生产构建应在 CMakeLists.txt 中 add_compile_definitions(SANYI_ENABLE_LICENSE)。
#ifdef SANYI_ENABLE_LICENSE
        License_SetCheckEnabled(1);
        SY_INFO("[CADApplicationRuntime] License check ENABLED via SANYI_ENABLE_LICENSE macro");
#endif

        if (License_IsCheckEnabled())
        {
            SY_INFO("[CADApplicationRuntime] License check enabled, verifying license");

            LicenseContext* licenseCtx = License_Create(&config);
            const bool licenseOk = licenseCtx && License_Check(licenseCtx) == LICENSE_OK;
            if (licenseCtx)
            {
                License_Destroy(licenseCtx);
            }

            if (!licenseOk)
            {
                SY_INFO("[CADApplicationRuntime] License check failed, showing license dialog");
                LicenseDialog dlg(configDir);
                if (dlg.exec() != QDialog::Accepted)
                {
                    SY_WARN("[CADApplicationRuntime] License check rejected by user");
                    return -3;
                }
                SY_INFO("[CADApplicationRuntime] License accepted by user");
            }
            else
            {
                SY_INFO("[CADApplicationRuntime] License check passed");
            }
        }
        else
        {
            SY_INFO("[CADApplicationRuntime] License check disabled (SANYI_ENABLE_LICENSE not defined)");
        }
    }

    // 创建应用引导器并设置启动工作台
    m_bootstrapper = std::make_unique<AppBootstrapper>(m_appPaths, MainApp::appName(), MainApp::appVersion());
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

    // 退出时自动保存 common 设置（字体/主题/语言）到 SQLite 数据库
    QObject::connect(m_app.get(), &QCoreApplication::aboutToQuit, []() {
        if (auto* svc = ApplicationCompositionRoot::getSettingsService())
        {
            svc->saveCurrentCommonSettings();
        }
    });

    const int exitCode = m_app->exec();
    SY_INFOF("[CADApplicationRuntime] Application exited with code %d", exitCode);
    return exitCode;
}

void CADApplicationRuntime::setStartWorkbenchId(const QString& workbenchId)
{
    m_startWorkbenchId = workbenchId;
}