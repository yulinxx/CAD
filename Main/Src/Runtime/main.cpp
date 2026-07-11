#include "CADApplicationRuntime.h"

#include <QFileInfo>

#include "Log/SyLogger.h"

#include "VersionInfo.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int runCADApplication(int argc, char** argv)
{
    auto appPaths = MainApp::buildAppPaths(MainApp::appName());
    if (appPaths.appRootPath.empty())
    {
#ifdef _WIN32
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) > 0)
        {
            appPaths.appRootPath = QFileInfo(QString::fromWCharArray(path)).absolutePath().toStdWString();
        }
#else
        if (argc > 0 && argv && argv[0])
        {
            appPaths.appRootPath = QFileInfo(QString::fromLocal8Bit(argv[0])).absolutePath().toStdWString();
        }
#endif
    }

    if (appPaths.appRootPath.empty())
    {
        SyLogger::GetInstance().Initialize("SanYiCAD");
        SY_ERROR("[main] error code=app.root_path_empty message=Application root path is empty");
        SyLogger::GetInstance().Shutdown();
        return -1;
    }

    CADApplicationRuntime runtime(argc, argv, appPaths);
    runtime.setStartWorkbenchId(QStringLiteral("2D"));
    return runtime.run();
}
