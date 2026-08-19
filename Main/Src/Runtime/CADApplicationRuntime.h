#pragma once

#include <memory>
#include <QString>

#include "../Bootstrap/AppBootstrapper.h"

class QApplication;

class CADApplicationRuntime
{
public:
    // QApplication 必须由调用方在 buildAppPaths() 之前创建并传入，
    // 否则 AppPathManager::applicationDirPath() 会在 QApplication 存在前被调用而告警。
    CADApplicationRuntime(std::unique_ptr<QApplication> app, const AppPaths& appPaths);
    ~CADApplicationRuntime();

public:
    int run();
    void setStartWorkbenchId(const QString& workbenchId);  // 设置启动时使用的工作台ID

private:
    std::unique_ptr<QApplication> m_app;
    std::unique_ptr<AppBootstrapper> m_bootstrapper;

    AppPaths m_appPaths;

    QString m_startWorkbenchId{ QStringLiteral("2D") };
};
