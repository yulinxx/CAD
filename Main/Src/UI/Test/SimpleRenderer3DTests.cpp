#include <gtest/gtest.h>
#include "UI/SimpleRenderer3D.h"
#include <QPainter>
#include <QImage>

TEST(SimpleRenderer3DTest, Lifecycle)
{
    SimpleRenderer3D renderer;

    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    bool initialized = renderer.initialize();
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
}

TEST(SimpleRenderer3DTest, RenderLoopControl)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_FALSE(renderer.isRenderLoopRunning());

    renderer.setRenderLoopEnabled(true);
    EXPECT_TRUE(renderer.isRenderLoopRunning());

    renderer.setRenderLoopEnabled(false);
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, OrbitMode)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.setOrbitMode(false);
    EXPECT_FALSE(renderer.isOrbitMode());

    renderer.setOrbitMode(true);
    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, ResetView)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setOrbitMode(true);
    renderer.resetView();

    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, SelectionGetters)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_EQ(renderer.selectedNodeId(), QString());
    EXPECT_TRUE(renderer.selectedPathNames().isEmpty());

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, MeasureMode)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.setMeasureMode(true);
    renderer.setMeasureMode(false);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, Render)
{
    GTEST_SKIP() << "Render test requires Qt GUI context";
}

TEST(SimpleRenderer3DTest, Resize)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.resize(800, 600);
    renderer.resize(1024, 768);

    renderer.shutdown();
}

TEST(SimpleRenderer3DTest, MouseEvents)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.onMousePress(100, 100, 1, 0, 640, 480);
    renderer.onMouseMove(150, 150, 1, 640, 480);
    renderer.onMouseRelease(150, 150, 1, 640, 480);

    renderer.onWheel(50, 640, 480);
    renderer.onWheel(-50, 640, 480);

    renderer.shutdown();
}