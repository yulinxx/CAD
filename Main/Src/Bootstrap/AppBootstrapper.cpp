/**
 * @file AppBootstrapper.cpp
 * @brief 应用程序引导器实现
 */

#include "AppBootstrapper.h"

#include <QDir>
#include <QString>

#include "../UI/UiWorkbench.h"

AppPaths MainApp::buildAppPaths(const std::string& appName)
{
    AppPaths paths;
    paths.appRootPath = AppPathManager::appRootDir().toStdString();
    paths.configDir = AppPathManager::configDir().toStdString();
    paths.resourcesDir = AppPathManager::resourcesDir().toStdString();
    paths.pluginsDir = AppPathManager::pluginsDir().toStdString();
    return paths;
}

AppBootstrapper::AppBootstrapper(const AppPaths& paths, const std::string& appName, const std::string& version)
    : m_paths(paths)
    , m_appName(appName)
    , m_version(version)
{
}

AppBootstrapper::~AppBootstrapper() = default;

bool AppBootstrapper::initialize()
{
    m_compositionRoot = std::make_unique<ApplicationCompositionRoot>();
    if (!m_compositionRoot || !m_compositionRoot->stateCenter() || !m_compositionRoot->themeService() || !m_compositionRoot->layoutService() || !m_compositionRoot->commandDispatcher() || !m_compositionRoot->shellHost())
        return false;
    return true;
}

void AppBootstrapper::bootstrap()
{
    if (!m_compositionRoot)
        return;

    m_services = { m_compositionRoot->stateCenter(), m_compositionRoot->themeService(), m_compositionRoot->layoutService(), m_compositionRoot->commandDispatcher() };

    if (m_compositionRoot->stateCenter())
        m_compositionRoot->stateCenter()->setCurrentWorkbenchId(m_startWorkbenchId);

    if (m_startWorkbenchId == QStringLiteral("3D"))
        m_workbench = std::make_unique<Workbench3D>();
    else
        m_workbench = std::make_unique<Workbench2D>();

    if (!m_workbench->initialize(m_services))
        return;

    auto* shell = m_compositionRoot->shellHost();
    shell->setWorkbench(m_workbench.get());
    shell->initializeAndShow();
}

void AppBootstrapper::shutdown()
{
    if (m_workbench)
        m_workbench->shutdown();
    m_workbench.reset();
}

QString AppBootstrapper::startWorkbenchId() const
{
    return m_startWorkbenchId;
}

ApplicationCompositionRoot* AppBootstrapper::compositionRoot()
{
    return m_compositionRoot.get();
}

const AppPaths& AppBootstrapper::appPaths() const
{
    return m_paths;
}

void AppBootstrapper::setStartWorkbenchId(const QString& workbenchId)
{
    m_startWorkbenchId = workbenchId;
}