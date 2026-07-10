/**
 * @file BackendE2ETests.cpp
 * @brief 渲染后端端到端测试
 *
 * 覆盖真实渲染场景的完整流程：
 * - 后端创建 → 初始化 → 帧提交 → 资源释放
 * - 多种后端类型的行为一致性
 * - 配置驱动的后端选择
 * - 多帧连续渲染稳定性
 * - 后端能力查询与验证
 *
 * 与单元测试的区别：
 * - 单元测试：测试单个组件的独立功能
 * - 集成测试：测试组件间协作
 * - 端到端测试：测试完整渲染管线的真实行为
 */

#include <gtest/gtest.h>

#include <QImage>
#include <QPainter>

#include "RenderCore/IRenderBackend.h"
#include "RenderCore/RenderCoreRenderer.h"
#include "RenderCore/RenderBackendFactory.h"
#include "RenderCore/DefaultSceneCompiler.h"
#include "RenderCore/UiCamera3D.h"
#include "UI/UiEntities.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Ut/Vec.h"

// ============================================================================
// 端到端测试：完整渲染管线
// ============================================================================

TEST(BackendE2ETest, FullRenderPipeline_SoftwareBackend)
{
    RenderCoreRenderer renderer;
    renderer.initialize();
    ASSERT_TRUE(renderer.isReady());

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    renderer.resize(800, 600);

    for (int i = 0; i < 3; ++i)
    {
        renderer.render(painter, 800, 600);
    }

    painter.end();
    renderer.shutdown();
}

TEST(BackendE2ETest, FullRenderPipeline_WithScene)
{
    RenderCoreRenderer renderer;
    renderer.initialize();
    ASSERT_TRUE(renderer.isReady());

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

// ============================================================================
// 端到端测试：后端创建与配置
// ============================================================================

TEST(BackendE2ETest, ConfiguredBackend_Lifecycle)
{
    auto backend = RenderBackendFactory::createConfigured();
    ASSERT_NE(backend, nullptr);

    EXPECT_FALSE(backend->isReady());

    bool initialized = backend->initialize();
    EXPECT_TRUE(initialized);
    EXPECT_TRUE(backend->isReady());

    backend->shutdown();
    EXPECT_FALSE(backend->isReady());
}

TEST(BackendE2ETest, AvailableBackends_Creation)
{
    auto types = RenderBackendFactory::availableBackends();
    ASSERT_FALSE(types.isEmpty());

    for (auto type : types)
    {
        auto backend = RenderBackendFactory::create(type);
        ASSERT_NE(backend, nullptr);
        EXPECT_FALSE(backend->backendName().isEmpty());
    }
}

TEST(BackendE2ETest, BackendCapability_Consistency)
{
    for (auto type : { RenderBackendFactory::BackendType::OpenGL,
                       RenderBackendFactory::BackendType::Software })
    {
        auto backend = RenderBackendFactory::create(type);
        ASSERT_NE(backend, nullptr);

        auto caps = RenderBackendFactory::capabilitiesFor(type);
        EXPECT_EQ(backend->supportsCapability(BackendCapability::HardwareAccelerated),
                  hasCapability(caps, BackendCapability::HardwareAccelerated));
    }
}

// ============================================================================
// 端到端测试：场景编译与渲染闭环
// ============================================================================

TEST(BackendE2ETest, SceneCompiler_2DEntities)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(line.release());

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(50, 50);
    circle->dRadius = 30.0;
    scene.addEntity(circle.release());

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    RenderFrame frame = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 2);

    ctx.clearDirty();
    RenderFrame cached = compiler.compile(&scene, ctx);
    EXPECT_EQ(cached.batchCount(), 2);
    EXPECT_LT(cached.statistics.compileTimeMs, 0.01);
}

TEST(BackendE2ETest, SceneCompiler_3DNodes)
{
    DefaultSceneCompiler compiler;
    SceneDocument3D doc;

    doc.createNode(QStringLiteral("Node1"));
    doc.createNode(QStringLiteral("Node2"));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("3D");
    ctx.clearDirty();

    RenderFrame frame = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 2);
}

// ============================================================================
// 端到端测试：相机交互与渲染反馈
// ============================================================================

TEST(BackendE2ETest, CameraInteraction_FullSequence)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);

    camera.onMousePress(400, 300, 1, 0, 800, 600);
    EXPECT_TRUE(camera.isRotating());

    camera.onMouseMove(500, 350, 1, 800, 600);
    EXPECT_TRUE(camera.isDirty());

    camera.onMouseRelease(500, 350, 1, 800, 600);
    EXPECT_FALSE(camera.isRotating());

    camera.onWheel(240, 800, 600);
    EXPECT_TRUE(camera.isDirty());

    camera.reset();
    EXPECT_DOUBLE_EQ(camera.yaw(), 0.0);
    EXPECT_DOUBLE_EQ(camera.pitch(), 15.0);
    EXPECT_DOUBLE_EQ(camera.distance(), 10.0);
}

// ============================================================================
// 端到端测试：多渲染器并行
// ============================================================================

TEST(BackendE2ETest, MultiRenderer_IndependentOperation)
{
    RenderCoreRenderer renderer1;
    RenderCoreRenderer renderer2;

    renderer1.initialize();
    renderer2.initialize();

    EXPECT_TRUE(renderer1.isReady());
    EXPECT_TRUE(renderer2.isReady());

    QImage image1(800, 600, QImage::Format_ARGB32);
    QImage image2(1280, 720, QImage::Format_ARGB32);

    QPainter painter1(&image1);
    QPainter painter2(&image2);

    renderer1.resize(800, 600);
    renderer2.resize(1280, 720);

    renderer1.render(painter1, 800, 600);
    renderer2.render(painter2, 1280, 720);

    painter1.end();
    painter2.end();

    renderer1.shutdown();
    renderer2.shutdown();
}

// ============================================================================
// 端到端测试：资源管理与稳定性
// ============================================================================

TEST(BackendE2ETest, ResourceManagement_MultipleInitializeShutdown)
{
    RenderCoreRenderer renderer;

    for (int i = 0; i < 3; ++i)
    {
        renderer.initialize();
        EXPECT_TRUE(renderer.isReady());

        QImage image(640, 480, QImage::Format_ARGB32);
        QPainter painter(&image);
        renderer.render(painter, 640, 480);
        painter.end();

        renderer.shutdown();
        EXPECT_FALSE(renderer.isReady());
    }
}

TEST(BackendE2ETest, Stability_LongRenderSequence)
{
    RenderCoreRenderer renderer;
    renderer.initialize();
    ASSERT_TRUE(renderer.isReady());

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    renderer.resize(800, 600);

    for (int i = 0; i < 10; ++i)
    {
        renderer.render(painter, 800, 600);

        if (i % 3 == 0)
        {
            renderer.onMousePress(400, 300, 1, 0, 800, 600);
            renderer.onMouseMove(410, 310, 1, 800, 600);
            renderer.onMouseRelease(410, 310, 1, 800, 600);
        }
        else if (i % 3 == 1)
        {
            renderer.onWheel(120, 800, 600);
        }
    }

    painter.end();
    renderer.shutdown();
}
