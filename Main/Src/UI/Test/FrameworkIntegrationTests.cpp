/**
 * @file FrameworkIntegrationTests.cpp
 * @brief 框架启动/切换/退出集成测试
 *
 * 覆盖完整渲染管线的真实场景：
 * - 启动阶段：渲染器初始化、编译缓存建立
 * - 切换阶段：2D/3D 场景切换、后端切换
 * - 退出阶段：资源释放、多次关闭幂等性
 * - 渲染闭环：输入事件 → 脏标记 → 增量编译 → 渲染输出
 * - 双屏场景：多渲染器并行、独立缓存
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <QImage>
#include <QPainter>

#include "RenderCore/RenderCoreRenderer.h"
#include "RenderCore/DefaultSceneCompiler.h"
#include "RenderCore/RenderBackendFactory.h"
#include "RenderCore/IRenderBackend.h"
#include "RenderCore/ViewCamera3D.h"
#include "UI/UiEntities.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Ut/Vec.h"

 // ============================================================================
 // 启动/关闭集成测试
 // ============================================================================

TEST(FrameworkIntegrationTest, Startup_InitializeAndShutdown)
{
    RenderCoreRenderer renderer;

    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    bool initialized = renderer.initialize();
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());
}

TEST(FrameworkIntegrationTest, Startup_ShutdownIdempotent)
{
    RenderCoreRenderer renderer;
    renderer.initialize();
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
    renderer.shutdown();
    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
}

TEST(FrameworkIntegrationTest, Startup_Sequence)
{
    RenderCoreRenderer renderer;

    renderer.initialize();
    EXPECT_TRUE(renderer.isReady());

    renderer.resize(800, 600);

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);
    renderer.render(painter, 800, 600);
    painter.end();

    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
}

// ============================================================================
// 2D/3D 切换集成测试
// ============================================================================

TEST(FrameworkIntegrationTest, Switch_2DSceneSwitch)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene1;
    auto line1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene1.addEntity(line1.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene1, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);
    EXPECT_EQ(frame1.entityCount(), 1);

    Eg::SceneManager scene2;
    auto circle2 = std::make_unique<Eg::SyCircle>();
    circle2->basePoint = Ut::Vec2d(50, 50);
    circle2->dRadius = 30.0;
    scene2.addEntity(circle2.release());

    compiler.invalidateCache();
    RenderFrame frame2 = compiler.compile(&scene2, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_EQ(frame2.entityCount(), 1);
}

TEST(FrameworkIntegrationTest, Switch_3DSceneSwitch)
{
    DefaultSceneCompiler compiler;
    SceneDocument3D doc1;
    doc1.createNode("Node1");

    RenderContext ctx;
    ctx.sceneType = "3D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&doc1, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);

    SceneDocument3D doc2;
    doc2.createNode("Node2");
    doc2.createNode("Node3");

    compiler.invalidateCache();
    RenderFrame frame2 = compiler.compile(&doc2, ctx);
    EXPECT_EQ(frame2.batchCount(), 2);
}

TEST(FrameworkIntegrationTest, Switch_BackendSwitchStability)
{
    auto opengl = RenderBackendFactory::create(RenderBackendFactory::BackendType::OpenGL);
    ASSERT_NE(opengl, nullptr);

    auto software = RenderBackendFactory::create(RenderBackendFactory::BackendType::Software);
    ASSERT_NE(software, nullptr);

    bool openglReady = opengl->initialize();
    bool softwareReady = software->initialize();

    if (openglReady)
    {
        EXPECT_TRUE(opengl->isReady());
        opengl->shutdown();
        EXPECT_FALSE(opengl->isReady());
    }

    if (softwareReady)
    {
        EXPECT_TRUE(software->isReady());
        software->shutdown();
        EXPECT_FALSE(software->isReady());
    }
}

// ============================================================================
// 脏更新渲染集成测试
// ============================================================================

TEST(FrameworkIntegrationTest, DirtyUpdate_SingleEntityDirty)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    auto lineId = line->id;
    scene.addEntity(line.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);

    auto* entity = scene.findEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    entity->transform(Ut::Mat3d::translate(50, 50));
    compiler.markEntityDirty(std::to_string(lineId));
    ctx.clearDirty();
    ctx.advanceFrame();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_NE(frame2.frameId, frame1.frameId);
}

TEST(FrameworkIntegrationTest, DirtyUpdate_MultipleEntitiesDirty)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;

    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    auto id1 = l1->id;
    scene.addEntity(l1.release());

    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(200, 200), Ut::Vec2d(300, 300) });
    auto id2 = l2->id;
    scene.addEntity(l2.release());

    auto c = std::make_unique<Eg::SyCircle>();
    c->basePoint = Ut::Vec2d(50, 50);
    c->dRadius = 20.0;
    auto cid = c->id;
    scene.addEntity(c.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 3);

    scene.findEntityById(id1)->transform(Ut::Mat3d::translate(10, 10));
    static_cast<Eg::SyCircle*>(scene.findEntityById(cid))->dRadius = 30.0;

    compiler.markEntityDirty(std::to_string(id1));
    compiler.markEntityDirty(std::to_string(cid));
    ctx.clearDirty();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 3);
    EXPECT_TRUE(compiler.hasCachedFrame());
}

TEST(FrameworkIntegrationTest, DirtyUpdate_MarkAllDirty)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(line.release());

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(50, 50);
    circle->dRadius = 25.0;
    scene.addEntity(circle.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    compiler.compile(&scene, ctx);
    EXPECT_TRUE(compiler.hasCachedFrame());

    compiler.markAllDirty();
    ctx.clearDirty();

    RenderFrame frame = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame.batchCount(), 2);
    EXPECT_GT(frame.statistics.compileTimeMs, 0);
}

// ============================================================================
// 渲染闭环集成测试
// ============================================================================

TEST(FrameworkIntegrationTest, RenderLoop_InputToRender)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    renderer.resize(800, 600);

    renderer.render(painter, 800, 600);

    renderer.onMousePress(400, 300, 1, 0, 800, 600);
    renderer.onMouseMove(450, 320, 1, 800, 600);
    renderer.onMouseRelease(450, 320, 1, 800, 600);

    renderer.render(painter, 800, 600);

    painter.end();
    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, RenderLoop_MultipleFramesWithInteraction)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    renderer.resize(800, 600);

    for (int i = 0; i < 3; ++i)
    {
        renderer.render(painter, 800, 600);

        if (i == 1)
        {
            renderer.onMousePress(400, 300, 1, 0, 800, 600);
            renderer.onMouseMove(420, 310, 1, 800, 600);
            renderer.onMouseRelease(420, 310, 1, 800, 600);
        }
        else if (i == 2)
        {
            renderer.onWheel(120, 800, 600);
        }
    }

    painter.end();
    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, RenderLoop_CameraInteractionSequence)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);

    EXPECT_DOUBLE_EQ(camera.yaw(), 0.0);
    EXPECT_DOUBLE_EQ(camera.pitch(), 15.0);
    EXPECT_DOUBLE_EQ(camera.distance(), 10.0);

    camera.onMousePress(400, 300, 1, 0, 800, 600);
    EXPECT_TRUE(camera.isRotating());
    EXPECT_TRUE(camera.isDirty());

    camera.onMouseMove(500, 350, 1, 800, 600);
    EXPECT_TRUE(camera.isDirty());

    camera.onMouseRelease(500, 350, 1, 800, 600);
    EXPECT_FALSE(camera.isRotating());

    EXPECT_NE(camera.yaw(), 0.0);
    EXPECT_NE(camera.pitch(), 15.0);

    camera.onWheel(240, 800, 600);
    EXPECT_TRUE(camera.isDirty());
    EXPECT_NE(camera.distance(), 10.0);

    camera.reset();
    EXPECT_DOUBLE_EQ(camera.yaw(), 0.0);
    EXPECT_DOUBLE_EQ(camera.pitch(), 15.0);
    EXPECT_DOUBLE_EQ(camera.distance(), 10.0);
}

// ============================================================================
// 双屏场景集成测试
// ============================================================================

TEST(FrameworkIntegrationTest, DualScreen_IndependentRenderers)
{
    RenderCoreRenderer renderer1;
    RenderCoreRenderer renderer2;

    renderer1.initialize();
    renderer2.initialize();

    EXPECT_TRUE(renderer1.isReady());
    EXPECT_TRUE(renderer2.isReady());

    renderer1.resize(800, 600);
    renderer2.resize(1920, 1080);

    QImage image1(800, 600, QImage::Format_ARGB32);
    QImage image2(1920, 1080, QImage::Format_ARGB32);

    QPainter painter1(&image1);
    QPainter painter2(&image2);

    renderer1.render(painter1, 800, 600);
    renderer2.render(painter2, 1920, 1080);

    painter1.end();
    painter2.end();

    renderer1.shutdown();
    renderer2.shutdown();

    EXPECT_FALSE(renderer1.isReady());
    EXPECT_FALSE(renderer2.isReady());
}

TEST(FrameworkIntegrationTest, DualScreen_IndependentCameras)
{
    ViewCamera3D camera1;
    ViewCamera3D camera2;

    camera1.setViewportSize(800, 600);
    camera2.setViewportSize(1280, 720);

    camera1.orbit(30.0, 10.0);
    camera2.orbit(-45.0, 20.0);

    EXPECT_DOUBLE_EQ(camera1.yaw(), 30.0);
    EXPECT_DOUBLE_EQ(camera2.yaw(), -45.0);
    EXPECT_DOUBLE_EQ(camera1.pitch(), 25.0);
    EXPECT_DOUBLE_EQ(camera2.pitch(), 35.0);
}

// ============================================================================
// 完整渲染管线集成测试
// ============================================================================

TEST(FrameworkIntegrationTest, FullPipeline_2DWithEntities)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;

    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());

    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(50, 50), Ut::Vec2d(150, 50) });
    scene.addEntity(l2.release());

    auto c = std::make_unique<Eg::SyCircle>();
    c->basePoint = Ut::Vec2d(100, 100);
    c->dRadius = 40.0;
    scene.addEntity(c.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 3);
    EXPECT_GT(frame.totalVertexCount(), 0);
    EXPECT_GT(frame.statistics.compileTimeMs, 0);

    ctx.clearDirty();
    RenderFrame cachedFrame = compiler.compile(&scene, ctx);
    EXPECT_TRUE(cachedFrame.valid);
    EXPECT_EQ(cachedFrame.batchCount(), 3);
    EXPECT_LT(cachedFrame.statistics.compileTimeMs, 0.01);
}

TEST(FrameworkIntegrationTest, FullPipeline_3DWithNodes)
{
    DefaultSceneCompiler compiler;
    SceneDocument3D doc;

    doc.createNode("Root1");
    doc.createNode("Root2");
    doc.createNode("Root3");

    RenderContext ctx;
    ctx.sceneType = "3D";
    ctx.clearDirty();

    RenderFrame frame = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 3);
}

TEST(FrameworkIntegrationTest, FullPipeline_SelectionFeedback)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    auto lineId = line->id;
    scene.addEntity(line.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);
    EXPECT_FALSE(frame1.batches[0].selected);

    auto* storedEntity = scene.findEntityById(lineId);
    ASSERT_NE(storedEntity, nullptr);
    scene.selectEntity(storedEntity);

    compiler.invalidateCache();
    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_TRUE(frame2.batches[0].selected);
}

// ============================================================================
// 配置驱动后端切换测试
// ============================================================================

TEST(FrameworkIntegrationTest, Config_DefaultBackend)
{
    auto backend = RenderBackendFactory::createConfigured();
    EXPECT_NE(backend, nullptr);
    EXPECT_FALSE(backend->backendName().empty());
}

TEST(FrameworkIntegrationTest, Config_AvailableBackends)
{
    auto backends = RenderBackendFactory::availableBackends();
    EXPECT_FALSE(backends.empty());

    bool hasSoftware = false;
    bool hasOpenGL = false;
    for (auto type : backends)
    {
        if (type == RenderBackendFactory::BackendType::Software)
            hasSoftware = true;
        if (type == RenderBackendFactory::BackendType::OpenGL)
            hasOpenGL = true;
    }

    EXPECT_TRUE(hasSoftware || hasOpenGL);
}

TEST(FrameworkIntegrationTest, Config_StringRoundTrip)
{
    for (auto type : { RenderBackendFactory::BackendType::OpenGL,
                       RenderBackendFactory::BackendType::Vulkan,
                       RenderBackendFactory::BackendType::Metal,
                       RenderBackendFactory::BackendType::Software })
    {
        std::string name = RenderBackendFactory::toString(type);
        EXPECT_FALSE(name.empty());
        EXPECT_EQ(RenderBackendFactory::fromString(name), type);
    }
}