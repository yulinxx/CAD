#include "CADApplicationRuntime.h"

#include <QFileInfo>

#include "VersionInfo.h"

int runCADApplication(int argc, char** argv)
{
    auto appPaths = MainApp::buildAppPaths(MainApp::appName());
    if (appPaths.appRootPath.empty() && argc > 0 && argv && argv[0])
        appPaths.appRootPath = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toStdString();

    if (appPaths.appRootPath.empty())
        return -1;

    CADApplicationRuntime runtime(argc, argv, appPaths);
    runtime.setStartWorkbenchId(QStringLiteral("2D"));
    return runtime.run();
}
