/**
 * @file CADApplicationRuntime.cpp
 * @brief CAD 应用程序运行时实现
 */

#include "CADApplicationRuntime.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>

#include "../Common/AppInitializer.h"

CADApplicationRuntime::CADApplicationRuntime(int argc, char* argv[], const AppPaths& appPaths)
    : m_app(std::make_unique<QApplication>(argc, argv))
    , m_appPaths(appPaths)
{
    m_app->setApplicationName(QStringLiteral("SanYiCAD"));
    m_app->setApplicationVersion(QStringLiteral("1.0.0"));
    m_app->setOrganizationName(QStringLiteral("SanYi"));
    m_app->setOrganizationDomain(QStringLiteral("sanyi-cad.com"));
    if (!m_appPaths.appRootPath.empty())
        QDir::setCurrent(QString::fromStdString(m_appPaths.appRootPath));
}

CADApplicationRuntime::~CADApplicationRuntime() = default;

int CADApplicationRuntime::run()
{
    AppInitializer::initialize();
    m_bootstrapper = std::make_unique<AppBootstrapper>(m_appPaths, "SanYiCAD", "1.0.0");
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