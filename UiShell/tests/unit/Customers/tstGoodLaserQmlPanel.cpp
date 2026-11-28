#include <QtTest/QtTest>

#include <QApplication>
#include <QColor>
#include <QQuickItem>
#include <QQuickWidget>

#include "qml/QmlPanelWidget.h"
#include "UiShell/Contract/HostContext.h"

using namespace AA;
using GL::QmlPanelWidget;

namespace
{
    HostContext contextFor(const char* themeId)
    {
        HostContext context;
        context.strThemeId = QString::fromLatin1(themeId);
        context.strLanguageId = QStringLiteral("en_US");
        return context;
    }

    QColor rootColor(const QmlPanelWidget& panel)
    {
        return panel.rootObject() ? panel.rootObject()->property("color").value<QColor>() : QColor();
    }
}  // namespace

// GoodLaser 的 QML 面板：锁住 qrc 资源路径（前缀 /UiGoodLaser + 文件名，没有 qml/ 这一级），
// 以及上下文属性注入后 QML 跟随皮肤。资源路径写错时面板是空白，界面上很难发现。
class GoodLaserQmlPanelTest : public QObject
{
    Q_OBJECT

private slots:
    void loadsFromQrc();
    void followsHostTheme();
};

void GoodLaserQmlPanelTest::loadsFromQrc()
{
    QmlPanelWidget panel(contextFor(ThemeId::Light));
    QCOMPARE(panel.source(), QUrl(QStringLiteral("qrc:/GoodLaser/QmlPanel.qml")));
    QVERIFY2(panel.status() == QQuickWidget::Ready,
        qPrintable(panel.errors().isEmpty() ? QStringLiteral("not ready") : panel.errors().first().toString()));
    QVERIFY(panel.rootObject() != nullptr);
}

// 颜色取自 QmlPanel.qml：light #fff7ed，dark #2a1d12。
void GoodLaserQmlPanelTest::followsHostTheme()
{
    QmlPanelWidget panel(contextFor(ThemeId::Light));
    QVERIFY(panel.rootObject() != nullptr);
    QCOMPARE(rootColor(panel), QColor(QStringLiteral("#fff7ed")));

    panel.setHostContext(contextFor(ThemeId::Dark));
    QCOMPARE(rootColor(panel), QColor(QStringLiteral("#2a1d12")));
}

// 无显卡的 CI / offscreen 下用 Qt Quick 软件渲染；必须在 QApplication 之前设置。
int main(int argc, char** argv)
{
    if (qEnvironmentVariableIsEmpty("QT_QUICK_BACKEND"))
        qputenv("QT_QUICK_BACKEND", "software");

    QApplication app(argc, argv);
    GoodLaserQmlPanelTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tstGoodLaserQmlPanel.moc"
