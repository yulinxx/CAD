#pragma once

#include <memory>

#include <QString>

class QApplication;
#include "../Bootstrap/AppBootstrapper.h"

class CADApplicationRuntime
{
public:
    CADApplicationRuntime(int argc, char* argv[], const AppPaths& appPaths);
    ~CADApplicationRuntime();

public:
    int run();
    void setStartWorkbenchId(const QString& workbenchId);

private:
    std::unique_ptr<QApplication> m_app;
    std::unique_ptr<AppBootstrapper> m_bootstrapper;

    AppPaths m_appPaths;

    QString m_startWorkbenchId{ QStringLiteral("2D") };
};
