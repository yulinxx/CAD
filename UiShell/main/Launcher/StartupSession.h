#pragma once

#include <QString>

#include "UiShell/Assembly/IShellRuntime.h"

class QMainWindow;

// 启动会话  处理profile 的加载与窗口绑定
class StartupSession
{
public:
    explicit StartupSession(AA::IShellRuntime* runtime);

public:
    bool loadProfile(const QString& path);

    // attachWindow 接管标题栏与窗口框架
    bool attach(QMainWindow* window);

private:
    // 加载 profile
    bool loadFromPath(const QString& profilePath);

    // 报告错误信息
    static void reportError(const QString& message);

    // 打印诊断信息
    void printDiagnostics(const QString& profilePath) const;

private:
    AA::IShellRuntime* m_shellRT = nullptr;
};
