#pragma once

#include <memory>
#include <string>

#include <QString>

#include "../Composition/ApplicationCompositionRoot.h"
#include "../Common/AppPathManager.h"
#include "../UI/Services/UIServices.h"

class UiWorkbench;

struct AppPaths
{
    std::wstring appRootPath;
    std::wstring configDir;
    std::wstring resourcesDir;
    std::wstring pluginsDir;
};

namespace MainApp
{
    AppPaths buildAppPaths(const std::string& appName);
}

/**
 * @class AppBootstrapper
 * @brief 应用程序引导器类
 *
 * 定义了应用程序的引导器类，负责初始化应用程序的各个组件。
 * 协调应用程序的启动流程，包括：
 * - 创建组合根
 * - 初始化工作台
 * - 启动 UI Shell
 * - 处理应用程序退出
 */
class AppBootstrapper
{
public:
    AppBootstrapper(const AppPaths& paths, const std::string& appName, const std::string& version);
    ~AppBootstrapper();

public:
    bool initialize();
    void bootstrap();

    /// 关闭应用。幂等：显式调用与析构各会走一次，重复调用直接返回。
    void shutdown();

    ApplicationCompositionRoot* compositionRoot();
    const AppPaths& appPaths() const;
    void setStartWorkbenchId(const QString& workbenchId);
    QString startWorkbenchId() const;

private:
    AppPaths m_paths;
    std::string m_appName;
    std::string m_version;
    std::unique_ptr<ApplicationCompositionRoot> m_compositionRoot;
    std::unique_ptr<UiWorkbench> m_workbench;
    QString m_startWorkbenchId{ QStringLiteral("2D") };

    /// shutdown 是否已执行过。关闭路径有两条（应用运行时的显式调用与本类析构），
    /// 没有这个标记就会重复停设备、重复关 shell，日志里出现多轮
    /// 「Starting shutdown / Shutdown complete」，真正那一次反而分不出来。
    bool m_shutdownDone{ false };
};
