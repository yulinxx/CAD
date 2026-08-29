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
#include "UI/ClientConfig/UiConfigSelfCheck.h"
#include "UI/ClientConfig/UiFeatureGate.h"

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
    //
    // SANYI_DISABLE_CRASH_HANDLER=1 时跳过：Breakpad 在 macOS 上抢的是 Mach 异常端口，
    // 优先级高于 AddressSanitizer 的信号处理，装上它 ASan 就永远打不出 free 栈/use 栈，
    // 只剩一个 minidump。排查内存问题（ASan / Valgrind）时必须让它让位。
    if (qEnvironmentVariableIntValue("SANYI_DISABLE_CRASH_HANDLER") != 0)
    {
        SY_WARN("[CADApplicationRuntime] CrashHandler disabled by SANYI_DISABLE_CRASH_HANDLER");
    }
    else if (!CrashHandlerBootstrap::initialize(MainApp::appName(), MainApp::appVersion()))
    {
        SY_WARN("[CADApplicationRuntime] CrashHandler initialization failed, continuing without crash capture");
    }

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

            // [P0-3] 功能授权闸门接线：把注册码里的 features 读入 UiFeatureGate，
            // 供菜单/工具栏/状态栏/右键菜单按 feature 字段决定是否创建入口。
            // 在此之前 features 字段虽然被签发、被解析，但从未被任何 UI 消费，
            // License 实际退化成了「能不能启动」的二元开关。
            if (licenseOk && licenseCtx)
            {
                LicenseInfo info{};
                info.structSize = sizeof(LicenseInfo);
                if (License_GetInfo(licenseCtx, &info) == LICENSE_OK)
                {
                    UiFeatureGate::instance().loadFromLicenseString(QString::fromUtf8(info.features));
                    SY_INFOF("[CADApplicationRuntime] License features applied: customer='%s' expiry='%s'",
                        info.customerName,
                        info.expiryDate);
                }
                else
                {
                    // 取不到授权明细时保守放行，避免因读取失败把客户已购功能全部锁死
                    SY_WARN("[CADApplicationRuntime] License_GetInfo failed, feature gate stays unrestricted");
                    UiFeatureGate::instance().setUnrestricted(true);
                }
            }

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

                // 用户刚激活成功，重新读取授权集：此时 features 才是最终生效的那份
                LicenseContext* activatedCtx = License_Create(&config);
                if (activatedCtx)
                {
                    LicenseInfo info{};
                    info.structSize = sizeof(LicenseInfo);
                    if (License_Check(activatedCtx) == LICENSE_OK &&
                        License_GetInfo(activatedCtx, &info) == LICENSE_OK)
                    {
                        UiFeatureGate::instance().loadFromLicenseString(QString::fromUtf8(info.features));
                        SY_INFOF("[CADApplicationRuntime] License features applied after activation: customer='%s'",
                            info.customerName);
                    }
                    License_Destroy(activatedCtx);
                }
            }
            else
            {
                SY_INFO("[CADApplicationRuntime] License check passed");
            }
        }
        else
        {
            // 许可校验未启用（开发构建）：授权闸门保持无限制，否则所有带 feature
            // 标记的菜单项都会消失，开发时无法验证功能。
            UiFeatureGate::instance().setUnrestricted(true);
            SY_INFO("[CADApplicationRuntime] License check disabled (SANYI_ENABLE_LICENSE not defined), "
                    "feature gate unrestricted");
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

    // 配置可信性自检：CMake 开关 / JSON 配置 / License 授权 三侧交叉核对。
    // 必须放在 bootstrap() 之后 —— 客户配置是首次访问 UiConfigurationManager::shared()
    // 时才加载的，而那发生在菜单/工作台构建期间。
    UiConfigSelfCheck::runAndLogForCurrentClient();

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