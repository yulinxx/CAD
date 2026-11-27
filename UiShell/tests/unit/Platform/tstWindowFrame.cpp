#include <QtTest/QtTest>

#include "Platform/WindowFrame.h"
#include "Platform/HitTest.h"

using namespace RD;

// 平台层的纯逻辑测试：不依赖真实 HWND，offscreen 下就能跑。
// attach/Win32 样式/DPI 枚举等依赖真实窗口的部分，留给集成测试。
class WindowFrameTest : public QObject
{
    Q_OBJECT

private slots:
    // ---- 构造/可空状态 ----
    void frameCanBeCreatedWithoutWindow();
    void startSystemMoveReturnsFalseBeforeAttach();
    void startSystemResizeReturnsFalseBeforeAttach();
    void resizeBorderWidthIsZero();

    // ---- edgeBandWidth() 带宽计算 ----
    void edgeBandAt96Dpi();          // 基准 5px
    void edgeBandAt150PercentDpi();   // 150% 缩放 → 8px
    void edgeBandAt200PercentDpi();   // 200% 缩放 → 10px
    void edgeBandFlooredBelowMinimum(); // 50% 缩放算出 3px → 钳到 4px
    void edgeBandClampsZeroDpi();     // GetDpiForWindow 失败兜底 96

    // ---- hitTest() 边缘优先级 ----
    void hitTopLeftCorner();           // 同时满足 top + left → TopLeft
    void hitTopRightCorner();
    void hitBottomLeftCorner();
    void hitBottomRightCorner();
    void hitLeftEdgeOnly();
    void hitRightEdgeOnly();
    void hitTopEdgeOnly();
    void hitBottomEdgeOnly();
    void hitClientArea();              // 全部条件不满足 → Client
    void hitBandBoundaryIsExclusive();  // pt.x == band 不算 left（严格小于）

    // ---- hitTest() 最大化覆盖 ----
    void hitMaximizedAlwaysReturnsClient(); // 最大化后边缘检测全部失效
    void hitMaximizedCornerGetsClient();    // 即使在角落也不能返回 TopLeft
};

void WindowFrameTest::frameCanBeCreatedWithoutWindow()
{
    const std::unique_ptr<IWindowFrame> frame = createWindowFrame();
    QVERIFY(frame != nullptr);
}

void WindowFrameTest::startSystemMoveReturnsFalseBeforeAttach()
{
    const std::unique_ptr<IWindowFrame> frame = createWindowFrame();
    QVERIFY(!frame->startSystemMove());
}

void WindowFrameTest::startSystemResizeReturnsFalseBeforeAttach()
{
    const std::unique_ptr<IWindowFrame> frame = createWindowFrame();
    QVERIFY(!frame->startSystemResize(Qt::LeftEdge));
}

void WindowFrameTest::resizeBorderWidthIsZero()
{
    const std::unique_ptr<IWindowFrame> frame = createWindowFrame();
#if defined(Q_OS_WIN)
    // Windows 的缩放交给 WM_NCHITTEST，边距为 0。
    QCOMPARE(frame->resizeBorderWidth(), 0);
#else
    // 非 Windows 平台用 Qt 窗口系统的 startSystemResize，
    // 缩放边缘由事件过滤器驱动，需要 4px 像素的边缘带。
    QCOMPARE(frame->resizeBorderWidth(), 4);
#endif
}

// ---- edgeBandWidth ----

void WindowFrameTest::edgeBandAt96Dpi()
{
    QCOMPARE(edgeBandWidth(96), 5);
}

void WindowFrameTest::edgeBandAt150PercentDpi()
{
    // 5 * 144 / 96 = 7.5，四舍五入到 8。
    QCOMPARE(edgeBandWidth(144), 8);   // 144 = 96 * 1.5
}

void WindowFrameTest::edgeBandAt200PercentDpi()
{
    QCOMPARE(edgeBandWidth(192), 10);
}

void WindowFrameTest::edgeBandFlooredBelowMinimum()
{
    // 5 * 48 / 96 = 2.5 → 3，被 max(4, 3) 钳到 4。
    QCOMPARE(edgeBandWidth(48), 4);
}

void WindowFrameTest::edgeBandClampsZeroDpi()
{
    // GetDpiForWindow 失败返回 0 或 0，应该按 96 DPI 兜底。
    QCOMPARE(edgeBandWidth(0), 5);
    QCOMPARE(edgeBandWidth(-1), 5);
}

// ---- hitTest ----

namespace
{
    // 平台无关的测试输入：包装 hitTest 的标量参数，让用例保持可读。
    struct Point
    {
        int x;
        int y;
    };

    struct Rect
    {
        int width = 800;
        int height = 600;
    };

    Point pt(int x, int y)
    {
        return Point{ x, y };
    }

    Rect rect800x600()
    {
        return Rect{};
    }

    HitTestZone hitTest(const Point& p, const Rect& r, bool maximized, int band)
    {
        return RD::hitTestZone(p.x, p.y, r.width, r.height, maximized, band);
    }
} // namespace

void WindowFrameTest::hitTopLeftCorner()
{
    const int band = edgeBandWidth(96);   // 5
    QCOMPARE(hitTest(pt(2, 2), rect800x600(), false, band), HitTestZone::TopLeft);
    QCOMPARE(hitTest(pt(0, 0), rect800x600(), false, band), HitTestZone::TopLeft);
    QCOMPARE(hitTest(pt(4, 4), rect800x600(), false, band), HitTestZone::TopLeft);
}

void WindowFrameTest::hitTopRightCorner()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(798, 2), rect800x600(), false, band), HitTestZone::TopRight);
    QCOMPARE(hitTest(pt(799, 0), rect800x600(), false, band), HitTestZone::TopRight);
}

void WindowFrameTest::hitBottomLeftCorner()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(2, 598), rect800x600(), false, band), HitTestZone::BottomLeft);
}

void WindowFrameTest::hitBottomRightCorner()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(798, 598), rect800x600(), false, band), HitTestZone::BottomRight);
}

void WindowFrameTest::hitLeftEdgeOnly()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(2, 300), rect800x600(), false, band), HitTestZone::Left);
}

void WindowFrameTest::hitRightEdgeOnly()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(798, 300), rect800x600(), false, band), HitTestZone::Right);
}

void WindowFrameTest::hitTopEdgeOnly()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(400, 2), rect800x600(), false, band), HitTestZone::Top);
}

void WindowFrameTest::hitBottomEdgeOnly()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(400, 598), rect800x600(), false, band), HitTestZone::Bottom);
}

void WindowFrameTest::hitClientArea()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(400, 300), rect800x600(), false, band), HitTestZone::Client);
    // 紧贴边缘带内侧：不应该被误判为边缘。
    QCOMPARE(hitTest(pt(20, 20), rect800x600(), false, band), HitTestZone::Client);
    QCOMPARE(hitTest(pt(780, 580), rect800x600(), false, band), HitTestZone::Client);
}

void WindowFrameTest::hitBandBoundaryIsExclusive()
{
    // 条件是 pt.x < band（严格小于），== band 应算客户区。
    const int band = 5;
    QCOMPARE(hitTest(pt(band, 300), rect800x600(), false, band), HitTestZone::Client);
    QCOMPARE(hitTest(pt(band - 1, 300), rect800x600(), false, band), HitTestZone::Left);
}

// ---- 最大化覆盖 ----

void WindowFrameTest::hitMaximizedAlwaysReturnsClient()
{
    const int band = 5;
    QCOMPARE(hitTest(pt(2, 300), rect800x600(), true, band), HitTestZone::Client);   // 本应 Left
    QCOMPARE(hitTest(pt(798, 598), rect800x600(), true, band), HitTestZone::Client); // 本应 BottomRight
    QCOMPARE(hitTest(pt(400, 300), rect800x600(), true, band), HitTestZone::Client); // 本来就是 Client
}

void WindowFrameTest::hitMaximizedCornerGetsClient()
{
    // 最大化状态下，即使在四角也必须返回 Client，用户应该能拖动窗口还原，
    // 而不是不小心抓到一个角就变成了缩放操作。
    const int band = 5;
    QCOMPARE(hitTest(pt(0, 0), rect800x600(), true, band), HitTestZone::Client);
    QCOMPARE(hitTest(pt(799, 599), rect800x600(), true, band), HitTestZone::Client);
}

QTEST_MAIN(WindowFrameTest)

#include "tstWindowFrame.moc"