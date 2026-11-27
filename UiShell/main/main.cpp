#include <QApplication>

#include "Launcher/AppBootstrap.h"
#include "Window/MainWindow.h"

int main(int argc, char** argv)
{
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

    MainWindow window(QStringLiteral("MainWindow/" UI_CUSTOMER_NAME));
    AppBootstrap bootstrap;
    const int exitCode = bootstrap.start(&window);
    if (exitCode != 0)
        return exitCode;

    window.restoreWindowState();

    window.show();
    return app.exec();
}