#pragma once

#include <memory>

#include "UiShell/Assembly/IShellRuntime.h"
#include "UiShell/Contract/IApiObject.h"

class QMainWindow;
class CustomerModuleProvider;
class StartupSession;
class TranslationSetup;

class AppBootstrap : private RD::IShellListener
{
public:
    AppBootstrap();
    ~AppBootstrap() override;

    AppBootstrap(const AppBootstrap&) = delete;
    AppBootstrap& operator=(const AppBootstrap&) = delete;

public:
    int start(QMainWindow* window);

    RD::IShellRuntime* runtime() const
    {
        return m_shellRT.get();
    }

    RD::IFeatureFlagStore* featureFlags() const
    {
        return m_featureFlags.get();
    }

private:
    // 处理语言变更：窗口标题里的客户显示名按新语种重算
    void onLanguageChanged(const QString& languageId) override;

private:
    QMainWindow* m_window = nullptr;

    // 客户模块提供者：持有 CustomerModule::owner
    std::unique_ptr<CustomerModuleProvider> m_moduleProvider;
    std::unique_ptr<TranslationSetup> m_transSetup;

    // shell 创建的对象，ApiPtr 自动在 shell DLL 内 destroy()
    RD::ApiPtr<RD::IFeatureFlagStore> m_featureFlags;
    RD::ApiPtr<RD::IWidgetFactoryRegistry> m_widgetReg;
    RD::ApiPtr<RD::IDialogFactoryRegistry> m_dlgReg;
    RD::ApiPtr<RD::IShellRuntime> m_shellRT;

    std::unique_ptr<StartupSession> m_startupSession;
};
