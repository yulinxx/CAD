#include "CADApplicationRuntime.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include "VersionInfo.h"
#include "../Common/AppInitializer.h"
#include "../License/LicenseManager.h"
#include "../License/LicenseDialog.h"

CADApplicationRuntime::CADApplicationRuntime(int argc, char* argv[], const AppPaths& appPaths)
    : m_app(std::make_unique<QApplication>(argc, argv))
    , m_appPaths(appPaths)
{
    m_app->setApplicationName(QString::fromStdString(MainApp::appName()));
    m_app->setApplicationVersion(QString::fromStdString(MainApp::appVersion()));
    m_app->setOrganizationName(QString::fromStdString(MainApp::organizationName()));
    m_app->setOrganizationDomain(QString::fromStdString(MainApp::organizationDomain()));
    if (!m_appPaths.appRootPath.empty())
        QDir::setCurrent(QString::fromStdString(m_appPaths.appRootPath));
}

CADApplicationRuntime::~CADApplicationRuntime()
{
    if (m_bootstrapper)
        m_bootstrapper->shutdown();
    m_bootstrapper.reset();
    m_app.reset();
}

int CADApplicationRuntime::run()
{
    AppInitializer::initialize();

    if (IsLicenseCheckEnabled() && false)
    {
        LicenseManager licenseMgr(m_appPaths.configDir);
        if (!licenseMgr.CheckLicense())
        {
            LicenseDialog dlg(QString::fromStdString(m_appPaths.configDir));
            if (dlg.exec() != QDialog::Accepted)
                return -3;
        }
    }

    m_bootstrapper = std::make_unique<AppBootstrapper>(m_appPaths, MainApp::appName(), MainApp::appVersion());
    m_bootstrapper->setStartWorkbenchId(m_startWorkbenchId);
    if (!m_bootstrapper->initialize())
        return -2;
    m_bootstrapper->bootstrap();
    return m_app->exec();
}

void CADApplicationRuntime::setStartWorkbenchId(const QString& workbenchId)
{
    m_startWorkbenchId = workbenchId;
}
