/**
 * @file CADApplicationRuntime.h
 * @brief CAD 应用程序运行时 — 封装 QApplication 生命周期和启动引导
 */

#pragma once

#include <memory>

#include <QString>

class QApplication;
#include "../Bootstrap/AppBootstrapper.h"

/**
 * @brief CAD 应用程序运行时
 *
 * 封装 QApplication 的创建、配置和事件循环，
 * 并委托 AppBootstrapper 完成服务初始化和工作台启动。
 */
class CADApplicationRuntime
{
public:
    CADApplicationRuntime(int argc, char* argv[], const AppPaths& appPaths);
    ~CADApplicationRuntime();

    /// 启动应用程序事件循环，返回退出码
    int run();

    /// 设置启动时默认加载的工作台 ID
    void setStartWorkbenchId(const QString& workbenchId);

private:
    std::unique_ptr<QApplication> m_app;
    std::unique_ptr<AppBootstrapper> m_bootstrapper;
    AppPaths m_appPaths;
    QString m_startWorkbenchId{ QStringLiteral("2D") };
};
