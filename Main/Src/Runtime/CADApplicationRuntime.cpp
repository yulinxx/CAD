#include "CADApplicationRuntime.h"

#include <QApplication>
#include <QCheckBox>
#include <QDate>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QObject>
#include <QPushButton>
#include <QSettings>

#include <cstdio>

#include "Log/SyLogger.h"
#include "VersionInfo.h"
#include "Common/AppInitializer.h"
#include "Common/CrashHandlerBootstrap.h"
#include "License/LicenseDialog.h"
#include "License/LicenseDLL.h"
#include "License/TrialManager.h"
#include "Composition/ApplicationCompositionRoot.h"
#include "UI/ClientConfig/UiConfigSelfCheck.h"
#include "UI/ClientConfig/UiFeatureGate.h"

#include "UI/Settings/SettingsService.h"

// 接收已创建的 QApplication（须由调用方在 buildAppPaths 之前创建），并设置应用基本信息
CADApplicationRuntime::CADApplicationRuntime(std::unique_ptr<QApplication> app, const AppPaths& appPaths)
    : m_app(std::move(app))
    , m_appPaths(appPaths)
{
    m_app->setApplicationName(QString::fromStdString(MainApp::appName()));
    m_app->setApplicationVersion(QString::fromStdString(MainApp::appVersion()));
    // organizationName/Domain 已在 CADApplicationEntry 中 QApplication 创建前设置
    m_app->setWindowIcon(QIcon(":/ui/common/Icons/Help/theme.svg"));

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
    // 初始化应用基础服务
    AppInitializer::initialize();

    // 初始化崩溃处理
    //
    // SANYI_DISABLE_CRASH_HANDLER=1 时跳过：Breakpad 在 macOS 上抢的是 Mach 异常端口，
    // 优先级高于 AddressSanitizer 的信号处理，装上它 ASan 就永远打不出 free 栈/use 栈，
    // 只剩一个 minidump。排查内存问题（ASan / Valgrind）时必须让它让位。
    if (qEnvironmentVariableIntValue("SANYI_DISABLE_CRASH_HANDLER") != 0)
    {
        SY_DEBUG("[CADApplicationRuntime] CrashHandler disabled");
    }
    else if (!CrashHandlerBootstrap::initialize(MainApp::appName(), MainApp::appVersion()))
    {
        SY_WARN("[CADApplicationRuntime] CrashHandler initialization failed, continuing without crash capture");
    }

    // 许可校验：先初始化配置，再检查开关是否启用
    {
        LicenseConfig config{};
        License_ConfigInit(&config);
        const QString configDir = QString::fromStdWString(m_appPaths.configDir.wstring());
        const QByteArray configDirUtf8 = configDir.toUtf8();
        config.configDir = configDirUtf8.constData();

        // 通过编译期宏 SANYI_ENABLE_LICENSE 显式启用许可校验
        // 生产构建应在 CMakeLists.txt 中 add_compile_definitions(SANYI_ENABLE_LICENSE)

        // === Phase 1: 试用期检查 ===
        Trial_SetConfigDir(configDirUtf8.constData());

        int remainingDays = 0;
        const int trialResult = Trial_Check(&remainingDays);
        bool inTrialMode = false;  // 标记用户是否处于试用模式（未持有有效许可证）

        SY_DEBUGF("[CADApplicationRuntime] Trial check: result=%d, remainingDays=%d", trialResult, remainingDays);

        if (trialResult == -1)
        {
            // 试用期已结束：必须注册或退出
            int expiredChoice = -1;  // 0=Register, 1=Exit
            QMessageBox expiredBox(QMessageBox::Warning,
                QString::fromUtf8("Trial Expired"),
                QString::fromUtf8("Your trial period has ended.\n\nPlease register to continue using the software."),
                QMessageBox::NoButton,
                nullptr);
            QPushButton* regBtn = expiredBox.addButton(QString::fromUtf8("Register Now"), QMessageBox::AcceptRole);
            QPushButton* exitBtn = expiredBox.addButton(QString::fromUtf8("Exit"), QMessageBox::RejectRole);
            QObject::connect(regBtn, &QPushButton::clicked, [&expiredChoice]() { expiredChoice = 0; });
            QObject::connect(exitBtn, &QPushButton::clicked, [&expiredChoice]() { expiredChoice = 1; });
            expiredBox.exec();

            if (expiredChoice != 0)
            {
                SY_INFO("[CADApplicationRuntime] Trial expired: user chose to exit");
                return -1;
            }

            // 用户选择注册 → 激活成功才继续，否则退出
            LicenseDialog dlg(configDir);
            if (dlg.exec() != QDialog::Accepted)
            {
                SY_WARN("[CADApplicationRuntime] Trial expired: user cancelled activation, exiting");
                return -1;
            }
            SY_INFO("[CADApplicationRuntime] Trial expired: user activated license successfully");
            // 激活成功，继续执行下面的 License 检查来加载 features
        }
        else if (trialResult == 0 && remainingDays > 0)
        {
            // 试用期活跃：提示剩余天数，可选择注册或继续试用
            UiFeatureGate::instance().setUnrestricted(true);
            SY_INFOF("[CADApplicationRuntime] Trial active: %d days remaining", remainingDays);

            QString trialMsg = QString::fromUtf8("Trial Version\n\n"
                "You have %1 day(s) remaining in your trial period.\n\n"
                "You can continue using the trial version or activate a license now.")
                .arg(remainingDays);

            int trialChoice = -1;  // 0=Register, 1=Continue
            QMessageBox trialBox(QMessageBox::Information,
                QString::fromUtf8("Trial Period"),
                trialMsg,
                QMessageBox::NoButton,
                nullptr);
            QPushButton* regBtn = trialBox.addButton(QString::fromUtf8("Register Now"), QMessageBox::AcceptRole);
            QPushButton* contBtn = trialBox.addButton(QString::fromUtf8("Continue Trial"), QMessageBox::RejectRole);
            QObject::connect(regBtn, &QPushButton::clicked, [&trialChoice]() { trialChoice = 0; });
            QObject::connect(contBtn, &QPushButton::clicked, [&trialChoice]() { trialChoice = 1; });
            trialBox.exec();

            SY_DEBUGF("[CADApplicationRuntime] Trial dialog choice=%d", trialChoice);

            if (trialChoice == 0)
            {
                // 用户选择注册 → 激活成功则应用 License features，失败仍以试用模式继续
                LicenseDialog dlg(configDir);
                if (dlg.exec() == QDialog::Accepted)
                {
                    SY_INFO("[CADApplicationRuntime] Trial active: user activated license, upgrading to licensed mode");
                    // 激活成功，继续执行下面的 License 检查来加载 features
                }
                else
                {
                    SY_INFO("[CADApplicationRuntime] Trial active: user cancelled activation, continuing in trial mode");
                    inTrialMode = true;
                }
            }
            else
            {
                SY_INFO("[CADApplicationRuntime] Trial active: user chose to continue trial");
                inTrialMode = true;
            }
        }
        else
        {
            // 试用期异常状态（天数为0但未过期）：按试用模式处理
            SY_WARN("[CADApplicationRuntime] Trial check unexpected state, falling back to trial mode");
            inTrialMode = true;
            UiFeatureGate::instance().setUnrestricted(true);
        }

        // === Phase 2: License 检查（仅在用户未处于试用模式时执行） ===
        if (!inTrialMode && License_IsCheckEnabled())
        {
            LicenseContext* licenseCtx = License_Create(&config);
            const bool licenseOk = licenseCtx && License_Check(licenseCtx) == LICENSE_OK;

            if (licenseOk && licenseCtx)
            {
                // 有效许可证 → 应用 features
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
                // 没有有效许可证且用户未在试用期流程中注册 → 弹出激活对话框
                SY_DEBUG("[CADApplicationRuntime] License check failed, showing license dialog");
                LicenseDialog dlg(configDir);
                if (dlg.exec() != QDialog::Accepted)
                {
                    SY_WARN("[CADApplicationRuntime] License check rejected by user");
                    return -3;
                }
                SY_DEBUG("[CADApplicationRuntime] License accepted by user");

                // 激活成功，重新读取 features
                LicenseContext* activatedCtx = License_Create(&config);
                if (activatedCtx)
                {
                    LicenseInfo info{};
                    info.structSize = sizeof(LicenseInfo);
                    if (License_Check(activatedCtx) == LICENSE_OK && License_GetInfo(activatedCtx, &info) == LICENSE_OK)
                    {
                        UiFeatureGate::instance().loadFromLicenseString(QString::fromUtf8(info.features));
                        SY_INFOF("[CADApplicationRuntime] License features applied after activation: customer='%s'",
                            info.customerName);
                    }
                    License_Destroy(activatedCtx);
                }
            }
        }
        else if (!inTrialMode)
        {
            // 许可校验未启用（开发构建）：授权闸门保持无限制
            UiFeatureGate::instance().setUnrestricted(true);
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