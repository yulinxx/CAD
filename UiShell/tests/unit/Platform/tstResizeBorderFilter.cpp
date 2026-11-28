#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QMouseEvent>
#include <QWidget>

#include "Platform/ResizeBorderFilter.h"

using namespace AA;

// ResizeBorderFilter 是 Linux/macOS 边缘缩放的唯一入口：
// onStartResize 没被窗口框架赋值时，拖拽边缘只会改光标、不会缩放。
class ResizeBorderFilterTest : public QObject
{
    Q_OBJECT

private slots:
    void edgePressInvokesResizeHandler();
    void clientPressDoesNotInvokeHandler();
    void mouseMoveUpdatesCursor();
};

void ResizeBorderFilterTest::edgePressInvokesResizeHandler()
{
    QWidget window;
    QWidget border;
    border.resize(400, 300);

    ResizeBorderFilter filter;
    filter.handler.window = &window;
    filter.handler.resizeWidget = &border;
    border.installEventFilter(&filter);

    int calls = 0;
    Qt::Edges received;
    filter.handler.onStartResize = [&calls, &received](Qt::Edges edges) {
        ++calls;
        received = edges;
        return true;
    };

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&border, &press);

    QCOMPARE(calls, 1);
    QVERIFY(received.testFlag(Qt::LeftEdge));
    QVERIFY(received.testFlag(Qt::TopEdge));
}

void ResizeBorderFilterTest::clientPressDoesNotInvokeHandler()
{
    QWidget window;
    QWidget border;
    border.resize(400, 300);

    ResizeBorderFilter filter;
    filter.handler.window = &window;
    filter.handler.resizeWidget = &border;
    border.installEventFilter(&filter);

    int calls = 0;
    filter.handler.onStartResize = [&calls](Qt::Edges) {
        ++calls;
        return true;
    };

    QMouseEvent press(QEvent::MouseButtonPress, QPointF(200, 150), QPointF(200, 150),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&border, &press);

    QCOMPARE(calls, 0);
}

void ResizeBorderFilterTest::mouseMoveUpdatesCursor()
{
    QWidget window;
    QWidget border;
    border.resize(400, 300);
    // 光标只有在控件创建出平台窗口后才会真正落到 QWidget 上；
    // offscreen 平台下 show() 即可完成创建，不需要真实窗口。
    border.show();
    // 与 setResizeBorderWidget() 的实际配置保持一致：
    // 未按键移动要收到 MouseMove 必须开启 mouseTracking，否则窗口系统不会投递。
    border.setMouseTracking(true);

    ResizeBorderFilter filter;
    filter.handler.window = &window;
    filter.handler.resizeWidget = &border;
    border.installEventFilter(&filter);

    QMouseEvent move(QEvent::MouseMove, QPointF(399, 1), QPointF(399, 1),
        Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(&border, &move);

    QCOMPARE(border.cursor().shape(), Qt::SizeBDiagCursor);
}

QTEST_MAIN(ResizeBorderFilterTest)

#include "tstResizeBorderFilter.moc"
