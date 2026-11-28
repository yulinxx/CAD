#include <QApplication>
#include <QCoreApplication>
#include <QTimer>

#include "Launcher/AppBootstrap.h"
#include "Window/MainWindow.h"

#ifdef APR_PRODUCT_STARTUP
    #include "Startup/AprilShellStartup.h"
#endif

int main(int argc, char** argv)
{
#ifdef APR_PRODUCT_STARTUP
    aprilShellPreAppInit(argc, argv);
#endif

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);

    // 组织名 + 应用名决定 QSettings 的存放位置（Windows：HKCU\Software\<产品名>\<产品名>）。
    // UI_CUSTOMER_NAME 在各客户 preset 下自动注入，换客户代号不共享用户配置。
    QApplication::setOrganizationName(QStringLiteral(UI_CUSTOMER_NAME));
    QApplication::setApplicationName(QStringLiteral(UI_CUSTOMER_NAME));
    QApplication::setApplicationVersion(QStringLiteral(UI_APP_VERSION));

#ifdef APR_PRODUCT_STARTUP
    int smokeExitMs = 0;
    const int cliExit = aprilShellHandleCliFlags(argc, argv, &smokeExitMs);
    if (cliExit >= 0)
        return cliExit;

    const int startupExit = aprilShellPostAppInit(argc, argv);
    if (startupExit != 0)
        return startupExit;
#endif

    MainWindow window(QStringLiteral("MainWindow/" UI_CUSTOMER_NAME));
    AppBootstrap bootstrap;
    const int exitCode = bootstrap.start(&window);
    if (exitCode != 0)
        return exitCode;

#ifdef APR_PRODUCT_STARTUP
    aprilShellAfterBootstrap();
    if (smokeExitMs > 0)
        QTimer::singleShot(smokeExitMs, &app, &QCoreApplication::quit);
#endif

    window.restoreWindowState();

    window.show();
    const int result = app.exec();

#ifdef APR_PRODUCT_STARTUP
    aprilShellShutdown();
#endif
    return result;
}
