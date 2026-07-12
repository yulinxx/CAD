#include "CADApplicationRuntime.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include <cstdio>

#include "Log/SyLogger.h"
#include "VersionInfo.h"
#include "../Common/AppInitializer.h"
#include "../Common/CrashHandlerBootstrap.h"
#include "License/LicenseDLL.h"
#include "../License/LicenseDialog.h"

// 构造函数：初始化QApplication并设置应用基本信息
CADApplicationRuntime::CADApplicationRuntime(int argc, char* argv[], const AppPaths& appPaths)
    : m_app(std::make_unique<QApplication>(argc, argv))
    , m_appPaths(appPaths)
{
    SY_INFOF("[CADApplicationRuntime] Initializing application: name=%s, version=%s",
        MainApp::appName().c_str(), MainApp::appVersion().c_str());

    m_app->setApplicationName(QString::fromStdString(MainApp::appName()));
    m_app->setApplicationVersion(QString::fromStdString(MainApp::appVersion()));
    m_app->setOrganizationName(QString::fromStdString(MainApp::organizationName()));
    m_app->setOrganizationDomain(QString::fromStdString(MainApp::organizationDomain()));

    // 设置当前工作目录到应用根目录
    if (!m_appPaths.appRootPath.empty())
    {
        QDir::setCurrent(QString::fromStdWString(m_appPaths.appRootPath));
        SY_DEBUGF("[CADApplicationRuntime] Set current directory: %ls", m_appPaths.appRootPath.c_str());
    }

    if (!CrashHandlerBootstrap::initialize(MainApp::appName(), MainApp::appVersion()))
    {
        std::fprintf(stderr,
            "[CADApplicationRuntime] CrashHandler initialization failed, continuing without crash capture\n");
    }

    SY_INFO("[CADApplicationRuntime] QApplication created successfully");
}

// 析构函数：按顺序关闭引导器和应用初始化器
CADApplicationRuntime::~CADApplicationRuntime()
{
    SY_INFO("[CADApplicationRuntime] Application shutting down");

    if (m_bootstrapper)
    {
        SY_DEBUG("[CADApplicationRuntime] Shutting down bootstrapper");
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
    SY_INFO("[CADApplicationRuntime] Starting application runtime");

    // 初始化应用基础服务
    SY_DEBUG("[CADApplicationRuntime] Initializing application services");
    AppInitializer::initialize();

    // 执行许可证检查（如果启用）
    if (License_IsCheckEnabled())
    {
        SY_DEBUG("[CADApplicationRuntime] License check enabled, verifying license");

        LicenseConfig config{};
        License_ConfigInit(&config);
        const QString configDir = QString::fromStdWString(m_appPaths.configDir);
        const QByteArray configDirUtf8 = configDir.toUtf8();
        config.configDir = configDirUtf8.constData();

        LicenseContext* licenseCtx = License_Create(&config);
        const bool licenseOk = licenseCtx && License_Check(licenseCtx) == LICENSE_OK;
        if (licenseCtx)
        {
            License_Destroy(licenseCtx);
        }

        if (!licenseOk)
        {
            SY_DEBUG("[CADApplicationRuntime] License check failed, showing license dialog");
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

    // 创建应用引导器并设置启动工作台
    SY_INFOF("[CADApplicationRuntime] Creating bootstrapper: workbench=%s", m_startWorkbenchId.toUtf8().constData());
    m_bootstrapper = std::make_unique<AppBootstrapper>(m_appPaths, MainApp::appName(), MainApp::appVersion());
    m_bootstrapper->setStartWorkbenchId(m_startWorkbenchId);

    // 初始化引导器
    SY_DEBUG("[CADApplicationRuntime] Initializing bootstrapper");
    if (!m_bootstrapper->initialize())
    {
        SY_ERROR("[CADApplicationRuntime] error code=app.bootstrap_init_failed message=AppBootstrapper initialization failed");
        return -2;
    }
    SY_INFO("[CADApplicationRuntime] Bootstrapper initialized successfully");

    // 执行引导序列
    SY_DEBUG("[CADApplicationRuntime] Running bootstrap sequence");
    m_bootstrapper->bootstrap();

    // 验证引导结果
    if (!m_bootstrapper->compositionRoot())
    {
        SY_ERROR("[CADApplicationRuntime] error code=app.bootstrap_no_root message=Bootstrap completed without a valid composition root");
        return -2;
    }
    SY_INFO("[CADApplicationRuntime] Bootstrap completed successfully");

    // 进入Qt事件循环
    SY_INFO("[CADApplicationRuntime] Entering Qt event loop");
    const int exitCode = m_app->exec();
    SY_INFOF("[CADApplicationRuntime] Application exited with code %d", exitCode);
    return exitCode;
}

// 设置启动时使用的工作台ID
void CADApplicationRuntime::setStartWorkbenchId(const QString& workbenchId)
{
    m_startWorkbenchId = workbenchId;
}