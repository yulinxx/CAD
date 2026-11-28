#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include "Window/MainWindow.h"

// MainWindow 的窗口状态保存 / 恢复：写到临时 ini，不碰真实注册表或用户配置。
class MainWindowTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void nothingSavedKeepsDefaultSize();
    void savedSizeIsRestored();
    void groupsAreIndependent();

private:
    QTemporaryDir m_tmp;
};

void MainWindowTest::initTestCase()
{
    QVERIFY(m_tmp.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tmp.path());
    QCoreApplication::setOrganizationName(QStringLiteral("MantiSoftTest"));
    QCoreApplication::setApplicationName(QStringLiteral("tstMainWindow"));
}

void MainWindowTest::nothingSavedKeepsDefaultSize()
{
    MainWindow window(QStringLiteral("MainWindow/empty"));
    window.resize(700, 500);
    QVERIFY(!window.restoreWindowState());
    QCOMPARE(window.size(), QSize(700, 500));
}

void MainWindowTest::savedSizeIsRestored()
{
    {
        MainWindow window(QStringLiteral("MainWindow/T"));
        window.resize(640, 480);
        window.saveWindowState();
    }

    // 模拟下次启动：AppBootstrap::start() 先 resize 到默认尺寸，再恢复。
    MainWindow window(QStringLiteral("MainWindow/T"));
    window.resize(700, 500);
    QVERIFY(window.restoreWindowState());
    QCOMPARE(window.size(), QSize(640, 480));
}

void MainWindowTest::groupsAreIndependent()
{
    {
        MainWindow customerA(QStringLiteral("MainWindow/X"));
        customerA.resize(500, 400);
        customerA.saveWindowState();
    }

    MainWindow customerB(QStringLiteral("MainWindow/Y"));
    QVERIFY(!customerB.restoreWindowState());
}

QTEST_MAIN(MainWindowTest)

#include "tstMainWindow.moc"
