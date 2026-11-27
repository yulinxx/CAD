#include "Launcher/AppBootstrap.h"

#include <QMainWindow>
#include <QString>

#include <cstdio>

#include "Customer/CustomerModuleProvider.h"
#include "UiDialogs/AboutDialogs.h"
#include "i18n/TranslationSetup.h"
#include "Launcher/StartupSession.h"

#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/BuildFingerprint.h"
#include "UiShell/Contract/CustomerModule.h"
#include "UiShell/Contract/DialogRegistry.h"
#include "UiShell/Contract/FeatureFlag.h"
#include "UiShell/Contract/WidgetFactory.h"

namespace
{
    // 退出码常量
    const int kExitCustomerModule = 6;  // 客户模块 Qt 运行时 / 构建指纹不匹配
    const int kExitTranslations = 5;    // 翻译服务创建失败
    const int kExitShellRuntime = 4;    // UiShell 构建指纹不匹配或运行期创建失败
    const int kExitProfile = 2;         // profile 加载失败（已弹出诊断）
    const int kExitAttach = 3;          // attachWindow 失败

    const int kDefaultWidth = 1200;
    const int kDefaultHeight = 800;
}  // namespace

AppBootstrap::AppBootstrap() = default;

AppBootstrap::~AppBootstrap() = default;

int AppBootstrap::start(QMainWindow* window)
{
    if (!window)
        return kExitAttach;

    // 先加载翻译服务，再加载客户模块
    m_transSetup = std::make_unique<TranslationSetup>();
    if (!m_transSetup->create())
    {
        std::fprintf(stderr, "App: cannot create translation service\n");
        return kExitTranslations;
    }
    m_transSetup->installFromArguments();

    m_moduleProvider = std::make_unique<CustomerModuleProvider>();
    if (!m_moduleProvider->load())
    {
        const QString res = m_moduleProvider->lastError();
        if (res.isEmpty())
        {
            std::fprintf(stderr, "App: customer module rejected (Qt runtime or build fingerprint mismatch)\n");
        }
        else
        {
            std::fprintf(stderr, "App: customer module rejected: %s\n", res.toUtf8().constData());
        }
        return kExitCustomerModule;
    }

    // 指纹校验：宿主 / shell 必须同一次构建
    if (qstrcmp(RD::hostBuildFingerprint(), RD::shellBuildFingerprint()) != 0)
    {
        std::fprintf(stderr, "App: UiShell build fingerprint mismatch\n");
        return kExitShellRuntime;
    }

    // 创建 shell 提供的对象。
    m_featureFlags.reset(RD::createFeatureFlagStore());
    m_widgetReg.reset(RD::createWidgetFactoryRegistry());
    m_dlgReg.reset(RD::createDialogRegistry());

    // 框架默认对话工厂
    static RD::about::AboutDialogFactory kDefaultAbout;
    m_dlgReg->registerFactory(RD::about::kAboutDlgId, &kDefaultAbout);

    if (!m_featureFlags || !m_widgetReg || !m_dlgReg)
    {
        std::fprintf(stderr, "App: failed to create shell registries / feature flag store\n");
        return kExitShellRuntime;
    }

    // 注册客户面板与对话框工厂。
    const RD::CustomerModule& module = m_moduleProvider->module();
    for (const RD::WidgetFactoryBinding& binding : module.listWidgetFactoriesBind)
    {
        if (!binding.strFactoryId.isEmpty() && binding.factory)
            m_widgetReg->registerFactory(binding.strFactoryId, binding.factory);
    }
    for (const RD::DlgFactoryBinding& binding : module.listDlgFactoriesBind)
    {
        if (!binding.strDialogId.isEmpty() && binding.factory)
            m_dlgReg->registerFactory(binding.strDialogId, binding.factory);
    }

    // 组装 ShellServices
    RD::ShellServices services;
    services.titleBarFactory = module.titleBar;
    services.centerView = module.centerView;
    services.cmdCatalog = module.cmdCatalog;
    services.cmdInvoker = module.cmdInvoker;
    services.cmdState = module.cmdState;
    services.strStyleSeries = module.strStyleSeries;
    services.featureFlags = m_featureFlags.get();
    services.widgetReg = m_widgetReg.get();
    services.dlgReg = m_dlgReg.get();
    services.dialogBindings = module.listDialogBind;
    services.text = m_transSetup->textProvider();
    services.transService = m_transSetup->service();
    services.shellListener = this;

    m_shellRT.reset(RD::createShellRuntime(services));
    if (!m_shellRT)
    {
        std::fprintf(stderr, "App: createShellRuntime returned null\n");
        return kExitShellRuntime;
    }

    window->setWindowTitle(m_moduleProvider->title());
    window->resize(kDefaultWidth, kDefaultHeight);

    m_window = window;
    m_startupSession = std::make_unique<StartupSession>(m_shellRT.get());

    // 加载 ui.json
    if (!m_startupSession->loadProfile(module.strProfilePath))
        return kExitProfile;

    // 接管标题栏与窗口框架
    if (!m_startupSession->attach(window))
        return kExitAttach;

    return 0;
}

void AppBootstrap::onLanguageChanged(const QString&)
{
    if (m_window && m_moduleProvider)
        m_window->setWindowTitle(m_moduleProvider->title());
}