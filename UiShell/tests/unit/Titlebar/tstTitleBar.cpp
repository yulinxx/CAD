#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QLabel>
#include <QLayout>
#include <QMainWindow>
#include <QPalette>
#include <QPointer>
#include <QMouseEvent>
#include <QWindow>

#include <algorithm>

#include "TestHelpers.h"
#include "UiShell/Titlebar/ITitleBarMenuHost.h"
#include "UiShell/Titlebar/ITitleBarView.h"

using namespace RD;

namespace
{
    TitleBarSpec makeSpec()
    {
        TitleBarSpec spec;
        spec.strTitle = QStringLiteral("测试标题");
        spec.strIcon = QStringLiteral("◆");
        spec.listButtons = QStringList{ QStringLiteral("minimize"),
                                   QStringLiteral("maximize"),
                                   QStringLiteral("close") };
        spec.nHeight = 40;
        return spec;
    }

    // 鼠标事件经由 QWindow 分发，控件必须先有真实窗口句柄。
    void showAndWaitForHandle(QWidget& widget)
    {
        widget.show();
        QVERIFY(QTest::qWaitFor([&widget]() {
            return widget.windowHandle() != nullptr;
            }, 1000));
    }

    QAbstractButton* buttonOf(ITitleBarView* bar, const QString& buttonId)
    {
        if (!bar || !bar->widget())
            return nullptr;
        return bar->widget()->findChild<QAbstractButton*>(
            QStringLiteral("msui.titlebar.button.") + buttonId);
    }

    // QTest::mouseMove 不保证携带左键状态；拖动阈值测试需要显式构造带按键的移动事件。
    void sendMouseMove(QWidget* widget, const QPoint& localPos)
    {
        QMouseEvent event(QEvent::MouseMove, localPos, widget->mapToGlobal(localPos),
            Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(widget, &event);
    }

    int dragThreshold()
    {
        return (std::max)(1, QApplication::startDragDistance());
    }
} // namespace

class TitleBarTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- 正常路径 ----
    void normalizesDescription();
    void buildsButtonsInConfiguredOrder();
    void buttonsCallHost();
    void maximizeStateChangesGlyphAndTooltip();
    void titleAndIconFollowDescription();
    void doubleClickForwardsToggleMaximize();
    void singleClickDoesNotToggleMaximize();

    // ---- 拖动：只发意图 ----
    void dragBelowThresholdDoesNotRequest();
    void dragAboveThresholdRequestsOnce();
    void dragCanRestartAfterRelease();
    void buttonsAreNotAffectedByDrag();

    // ---- 颜色/调色板 ----
    void paletteUsesConfiguredBackground();
    void paletteUsesConfiguredTextColorAndPropagates();
    void invalidColorStringIsIgnored();

    // ---- 菜单容器 ----
    void menuWidgetSetsPropertiesAndShowsContainer();
    void menuWidgetReplaceDeletesPrevious();
    void menuWidgetNullHidesContainer();

    // ---- 高度边界截断 ----
    void heightBelowMinIsClamped();
    void heightAboveMaxIsClamped();

    // ---- icon / showTitle 分支 ----
    void iconNonexistentFileFallsBackToText();
    void showTitleFalseUsesStretchInstead();

    // ---- 平台约定属性 ----
    void exposesPlatformContractProperties();

    // ---- 皮肤下的实际绘制 ----
    void rendersConfiguredBackgroundUnderStyleSheet();
};

void TitleBarTest::normalizesDescription()
{
    TitleBarSpec spec;
    spec.strTitle = QStringLiteral("   ");
    spec.nHeight = 0;
    spec.listButtons = QStringList{ QStringLiteral("maximize"),
                               QStringLiteral("bogus"),
                               QStringLiteral("maximize"),
                               QStringLiteral("close") };

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);

    QCOMPARE(bar->titleText(), QStringLiteral("UI"));
    QCOMPARE(bar->widget()->height(), 36);
    QCOMPARE(bar->buttonIds().size(), 2);
    QCOMPARE(bar->buttonIds().at(0), QStringLiteral("maximize"));
    QCOMPARE(bar->buttonIds().at(1), QStringLiteral("close"));
    QVERIFY(buttonOf(bar.get(), QStringLiteral("bogus")) == nullptr);
}

void TitleBarTest::buildsButtonsInConfiguredOrder()
{
    TitleBarSpec spec = makeSpec();
    spec.listButtons = QStringList{ QStringLiteral("close"),
                               QStringLiteral("maximize"),
                               QStringLiteral("minimize") };

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);

    QCOMPARE(bar->buttonIds().size(), 3);
    QCOMPARE(bar->buttonIds().at(0), QStringLiteral("close"));
    QCOMPARE(bar->buttonIds().at(1), QStringLiteral("maximize"));
    QCOMPARE(bar->buttonIds().at(2), QStringLiteral("minimize"));

    auto* buttons = bar->widget()->findChild<QWidget*>(QStringLiteral("msui.titlebar.buttons"));
    QVERIFY(buttons != nullptr);
    QLayout* layout = buttons->layout();
    QVERIFY(layout != nullptr);
    QCOMPARE(layout->count(), 3);
    QCOMPARE(layout->itemAt(0)->widget()->objectName(),
        QStringLiteral("msui.titlebar.button.close"));
    QCOMPARE(layout->itemAt(1)->widget()->objectName(),
        QStringLiteral("msui.titlebar.button.maximize"));
    QCOMPARE(layout->itemAt(2)->widget()->objectName(),
        QStringLiteral("msui.titlebar.button.minimize"));
}

void TitleBarTest::buttonsCallHost()
{
    test::FakeTitleBarHost host;
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec(), &host));
    QVERIFY(bar);
    showAndWaitForHandle(*bar->widget());

    QAbstractButton* minimize = buttonOf(bar.get(), QStringLiteral("minimize"));
    QAbstractButton* maximize = buttonOf(bar.get(), QStringLiteral("maximize"));
    QAbstractButton* close = buttonOf(bar.get(), QStringLiteral("close"));
    QVERIFY(minimize != nullptr);
    QVERIFY(maximize != nullptr);
    QVERIFY(close != nullptr);

    QTest::mouseClick(minimize, Qt::LeftButton);
    QTest::mouseClick(maximize, Qt::LeftButton);
    QTest::mouseClick(close, Qt::LeftButton);

    QCOMPARE(host.minimizeCount, 1);
    QCOMPARE(host.toggleMaximizeCount, 1);
    QCOMPARE(host.closeCount, 1);
}

void TitleBarTest::maximizeStateChangesGlyphAndTooltip()
{
    test::FakeTitleBarHost host;
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec(), &host));
    QVERIFY(bar);

    auto* maximize = buttonOf(bar.get(), QStringLiteral("maximize"));
    QVERIFY(maximize != nullptr);

    bar->setMaximized(false);
    QCOMPARE(maximize->text(), QStringLiteral("\u25A1"));
    QCOMPARE(maximize->toolTip(), QStringLiteral("Maximize"));

    bar->setMaximized(true);
    QVERIFY(bar->isMaximizedShown());
    QCOMPARE(maximize->text(), QStringLiteral("\u2750"));
    QCOMPARE(maximize->toolTip(), QStringLiteral("Restore"));

    bar->setMaximized(false);
    QVERIFY(!bar->isMaximizedShown());
    QCOMPARE(maximize->text(), QStringLiteral("\u25A1"));
}

void TitleBarTest::titleAndIconFollowDescription()
{
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec()));
    QVERIFY(bar);

    auto* title = bar->widget()->findChild<QLabel*>(QStringLiteral("msui.titlebar.title"));
    auto* icon = bar->widget()->findChild<QLabel*>(QStringLiteral("msui.titlebar.icon"));
    QVERIFY(title != nullptr);
    QVERIFY(icon != nullptr);
    QCOMPARE(title->text(), QStringLiteral("测试标题"));
    QCOMPARE(icon->text(), QStringLiteral("◆"));
    QCOMPARE(bar->widget()->height(), 40);

    auto* minimize = buttonOf(bar.get(), QStringLiteral("minimize"));
    auto* close = buttonOf(bar.get(), QStringLiteral("close"));
    QVERIFY(minimize != nullptr);
    QVERIFY(close != nullptr);
    QCOMPARE(minimize->text(), QStringLiteral("\u2212"));
    QCOMPARE(minimize->toolTip(), QStringLiteral("Minimize"));
    QCOMPARE(close->text(), QStringLiteral("\u2715"));
    QCOMPARE(close->toolTip(), QStringLiteral("Close"));
}

void TitleBarTest::doubleClickForwardsToggleMaximize()
{
    test::FakeTitleBarHost host;
    TitleBarSpec spec = makeSpec();

    ApiPtr<ITitleBarView> bar(createTitleBar(spec, &host));
    QVERIFY(bar);
    QWidget* widget = bar->widget();
    widget->resize(400, spec.nHeight);
    showAndWaitForHandle(*widget);

    QTest::mouseDClick(widget, Qt::LeftButton, Qt::NoModifier, widget->rect().center());

    QCOMPARE(host.toggleMaximizeCount, 1);
}

void TitleBarTest::singleClickDoesNotToggleMaximize()
{
    test::FakeTitleBarHost host;
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec(), &host));
    QVERIFY(bar);
    QWidget* widget = bar->widget();
    widget->resize(400, 40);
    showAndWaitForHandle(*widget);

    QTest::mouseClick(widget, Qt::LeftButton, Qt::NoModifier, widget->rect().center());

    QCOMPARE(host.toggleMaximizeCount, 0);
    QCOMPARE(host.dragCount, 0);
}

void TitleBarTest::dragBelowThresholdDoesNotRequest()
{
    test::FakeTitleBarHost host;
    TitleBarSpec spec = makeSpec();

    QMainWindow mainWin;
    ApiPtr<ITitleBarView> bar(createTitleBar(spec, &host));
    QVERIFY(bar);
    QWidget* barWidget = bar->widget();
    barWidget->setParent(mainWin.menuWidget());
    mainWin.setMenuWidget(barWidget);
    mainWin.resize(800, 600);
    showAndWaitForHandle(mainWin);
    barWidget->resize(800, spec.nHeight);

    const QPoint startPos = mainWin.pos();
    const QPoint center = barWidget->rect().center();
    QTest::mousePress(barWidget, Qt::LeftButton, Qt::NoModifier, center);
    sendMouseMove(barWidget, center + QPoint(dragThreshold(), 0));
    QTest::mouseRelease(barWidget, Qt::LeftButton, Qt::NoModifier, center + QPoint(dragThreshold(), 0));

    QCOMPARE(host.dragCount, 0);
    QCOMPARE(mainWin.pos(), startPos);   // 视图不应自行移动窗口
}

void TitleBarTest::dragAboveThresholdRequestsOnce()
{
    test::FakeTitleBarHost host;
    TitleBarSpec spec = makeSpec();

    QMainWindow mainWin;
    ApiPtr<ITitleBarView> bar(createTitleBar(spec, &host));
    QVERIFY(bar);
    QWidget* barWidget = bar->widget();
    barWidget->setParent(mainWin.menuWidget());
    mainWin.setMenuWidget(barWidget);
    mainWin.resize(800, 600);
    showAndWaitForHandle(mainWin);
    barWidget->resize(800, spec.nHeight);

    const QPoint startPos = mainWin.pos();
    const QPoint center = barWidget->rect().center();
    QTest::mousePress(barWidget, Qt::LeftButton, Qt::NoModifier, center);
    sendMouseMove(barWidget, center + QPoint(dragThreshold() + 1, 0));
    sendMouseMove(barWidget, center + QPoint(dragThreshold() + 40, 20));
    QTest::mouseRelease(barWidget, Qt::LeftButton, Qt::NoModifier, center + QPoint(dragThreshold() + 40, 20));

    // 一次按下只发一次拖动意图；窗口位移由宿主/平台完成，视图不调用 move()。
    QCOMPARE(host.dragCount, 1);
    QCOMPARE(mainWin.pos(), startPos);
}

void TitleBarTest::dragCanRestartAfterRelease()
{
    test::FakeTitleBarHost host;
    TitleBarSpec spec = makeSpec();

    ApiPtr<ITitleBarView> bar(createTitleBar(spec, &host));
    QVERIFY(bar);
    QWidget* titleBarWidget = bar->widget();
    titleBarWidget->resize(400, spec.nHeight);
    showAndWaitForHandle(*titleBarWidget);

    const QPoint center = titleBarWidget->rect().center();
    for (int i = 0; i < 2; ++i)
    {
        QTest::mousePress(titleBarWidget, Qt::LeftButton, Qt::NoModifier, center);
        sendMouseMove(titleBarWidget, center + QPoint(dragThreshold() + 1, 0));
        QTest::mouseRelease(titleBarWidget, Qt::LeftButton, Qt::NoModifier, center + QPoint(dragThreshold() + 1, 0));
    }

    QCOMPARE(host.dragCount, 2);
}

void TitleBarTest::buttonsAreNotAffectedByDrag()
{
    test::FakeTitleBarHost host;
    TitleBarSpec spec = makeSpec();

    ApiPtr<ITitleBarView> bar(createTitleBar(spec, &host));
    QVERIFY(bar);
    QWidget* widget = bar->widget();
    widget->resize(400, spec.nHeight);
    showAndWaitForHandle(*widget);

    QAbstractButton* close = buttonOf(bar.get(), QStringLiteral("close"));
    QVERIFY(close != nullptr);

    QTest::mouseClick(close, Qt::LeftButton);

    QCOMPARE(host.closeCount, 1);
    QCOMPARE(host.dragCount, 0);
}

// ===== 颜色/调色板 =====

// backgroundColor 应该改 QPalette::Window 并启用 autoFillBackground。
void TitleBarTest::paletteUsesConfiguredBackground()
{
    TitleBarSpec spec = makeSpec();
    spec.strBkgColor = QStringLiteral("#1a1a1a");

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);
    QWidget* widget = bar->widget();

    QVERIFY(widget->autoFillBackground());
    const QPalette pal = widget->palette();
    QCOMPARE(pal.color(QPalette::Window), QColor("#1a1a1a"));
}

// textColor 应该改 QPalette::WindowText，且同步到 title label / icon label。
void TitleBarTest::paletteUsesConfiguredTextColorAndPropagates()
{
    TitleBarSpec spec = makeSpec();
    spec.strTextColor = QStringLiteral("#ffcc00");

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);
    QWidget* widget = bar->widget();

    const QColor expected("#ffcc00");
    QCOMPARE(widget->palette().color(QPalette::WindowText), expected);

    auto* title = widget->findChild<QLabel*>(QStringLiteral("msui.titlebar.title"));
    auto* icon = widget->findChild<QLabel*>(QStringLiteral("msui.titlebar.icon"));
    if (title) QCOMPARE(title->palette().color(QPalette::WindowText), expected);
    if (icon) QCOMPARE(icon->palette().color(QPalette::WindowText), expected);
}

// 非法颜色字符串（不是 #rgb / #rrggbb / 颜色名）应该被忽略，不覆盖调色板。
void TitleBarTest::invalidColorStringIsIgnored()
{
    QPalette defaultPal = QApplication::palette();

    TitleBarSpec spec = makeSpec();
    spec.strBkgColor = QStringLiteral("banana");
    spec.strTextColor = QStringLiteral("not-a-color");

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);
    QWidget* widget = bar->widget();

    QVERIFY(!widget->autoFillBackground());
    QCOMPARE(widget->palette().color(QPalette::Window), defaultPal.color(QPalette::Window));
}

// ===== 菜单容器 =====

// setMenuWidget 后容器应显示（自身 visible=true，不依赖祖先的 effective visibility）。
void TitleBarTest::menuWidgetSetsPropertiesAndShowsContainer()
{
    test::FakeTitleBarHost host;
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec(), &host));
    QVERIFY(bar);
    auto* menuHost = dynamic_cast<ITitleBarMenuHost*>(bar.get());
    QVERIFY(menuHost != nullptr);

    QWidget* container = bar->widget()->findChild<QWidget*>(QStringLiteral("msui.titlebar.menu"));
    QVERIFY(container != nullptr);
    QVERIFY(container->isHidden());   // 自身被 hide

    QWidget* menu = new QWidget();
    menuHost->setMenuWidget(menu);

    QVERIFY(!container->isHidden());   // 自身被 show

    delete menu;   // 手动清理
}

// setMenuWidget 替换时旧菜单应该被删除，防止不可见残留。
void TitleBarTest::menuWidgetReplaceDeletesPrevious()
{
    test::FakeTitleBarHost host;
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec(), &host));
    QVERIFY(bar);
    auto* menuHost = dynamic_cast<ITitleBarMenuHost*>(bar.get());
    QVERIFY(menuHost != nullptr);

    QWidget* firstMenu = new QWidget();
    menuHost->setMenuWidget(firstMenu);
    QPointer<QWidget> firstPtr(firstMenu);

    QWidget* secondMenu = new QWidget();
    menuHost->setMenuWidget(secondMenu);

    QTest::qWait(10);
    QVERIFY(firstPtr.isNull());   // 旧菜单已被 delete
    QVERIFY(secondMenu->parentWidget() != nullptr);
}

// setMenuWidget(nullptr) 应该隐藏容器（自身 visible 属性为 false）。
// 注意：setMenuWidget 内部已经 delete 旧菜单，不能再手动 delete。
void TitleBarTest::menuWidgetNullHidesContainer()
{
    test::FakeTitleBarHost host;
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec(), &host));
    QVERIFY(bar);
    auto* menuHost = dynamic_cast<ITitleBarMenuHost*>(bar.get());
    QVERIFY(menuHost != nullptr);

    QWidget* menu = new QWidget();
    menuHost->setMenuWidget(menu);
    QWidget* container = bar->widget()->findChild<QWidget*>(QStringLiteral("msui.titlebar.menu"));
    QVERIFY(!container->isHidden());

    menuHost->setMenuWidget(nullptr);
    QVERIFY(container->isHidden());
}

// ===== 高度边界 =====

void TitleBarTest::heightBelowMinIsClamped()
{
    TitleBarSpec spec = makeSpec();
    spec.nHeight = 23;

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);
    QCOMPARE(bar->widget()->height(), 24);
}

void TitleBarTest::heightAboveMaxIsClamped()
{
    TitleBarSpec spec = makeSpec();
    spec.nHeight = 120;

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);
    QCOMPARE(bar->widget()->height(), 96);
}

// ===== icon / showTitle =====

void TitleBarTest::iconNonexistentFileFallsBackToText()
{
    TitleBarSpec spec = makeSpec();
    spec.strIcon = QStringLiteral("/nonexistent/path/to/icon.png");

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);
    auto* iconLabel = bar->widget()->findChild<QLabel*>(QStringLiteral("msui.titlebar.icon"));
    QVERIFY(iconLabel != nullptr);

    // Qt6 的 QLabel::pixmap() 按值返回 QPixmap，未设置图标时为空图元
    const QPixmap pm = iconLabel->pixmap();
    QVERIFY(pm.isNull());
    QVERIFY(!iconLabel->text().isEmpty());
}

void TitleBarTest::showTitleFalseUsesStretchInstead()
{
    TitleBarSpec spec = makeSpec();
    spec.bShowTitle = false;

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);

    auto* title = bar->widget()->findChild<QLabel*>(QStringLiteral("msui.titlebar.title"));
    QVERIFY(title == nullptr);
}

// 平台层/宿主可依赖的稳定属性：标题栏语义角色与交互区标记。
void TitleBarTest::exposesPlatformContractProperties()
{
    ApiPtr<ITitleBarView> bar(createTitleBar(makeSpec()));
    QVERIFY(bar);
    QWidget* widget = bar->widget();

    QCOMPARE(widget->property("msuiRole").toString(), QStringLiteral("titlebar"));

    auto* buttons = widget->findChild<QWidget*>(QStringLiteral("msui.titlebar.buttons"));
    QVERIFY(buttons != nullptr);
}

// 皮肤 QSS 存在时 autoFillBackground 不保证绘制；这里验证 profile 配置的
// backgroundColor 真的落在像素上，而不是只改了 QPalette。
void TitleBarTest::rendersConfiguredBackgroundUnderStyleSheet()
{
    // 只保留会命中标题栏的规则：QSS 存在时 autoFillBackground 不再保证绘制，
    // 视图必须自己填充 profile 配置的背景色。
    qApp->setStyleSheet(
        QStringLiteral("QWidget[msuiRole=\"titlebar\"] { border-bottom: 1px solid #c6ccd6; }"));

    TitleBarSpec spec = makeSpec();
    spec.strBkgColor = QStringLiteral("#1E3A8A");
    spec.strTextColor = QStringLiteral("#FFFFFF");

    ApiPtr<ITitleBarView> bar(createTitleBar(spec));
    QVERIFY(bar);
    QWidget* widget = bar->widget();
    widget->resize(400, spec.nHeight);
    showAndWaitForHandle(*widget);

    const QImage image = widget->grab().toImage();
    QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), QColor("#1E3A8A"));

    qApp->setStyleSheet(QString());
}

QTEST_MAIN(TitleBarTest)

#include "tstTitleBar.moc"
