/**
 * @file main.cpp
 * @brief CAD 应用程序入口点
 */

#include "CADApplicationRuntime.h"

#include <QFileInfo>

int runCADApplication(int argc, char** argv)
{
    auto appPaths = MainApp::buildAppPaths("SanYiCAD");
    if (appPaths.appRootPath.empty() && argc > 0 && argv && argv[0])
        appPaths.appRootPath = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toStdString();

    if (appPaths.appRootPath.empty())
        return -1;

    CADApplicationRuntime runtime(argc, argv, appPaths);
    runtime.setStartWorkbenchId(QStringLiteral("2D"));
    return runtime.run();
}