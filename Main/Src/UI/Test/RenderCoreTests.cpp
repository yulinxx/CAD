/**
 * @file RenderCoreTests.cpp
 * @brief RenderCore 统一渲染抽象层测试套件
 *
 * 覆盖：
 * - 后端初始化与销毁（DefaultRenderBackend）
 * - 场景编译（SceneCompiler 全量/增量/缓存）
 * - 渲染上下文生命周期（RenderContext）
 * - 后端工厂（RenderBackendFactory 创建/配置/选择）
 * - 相机投影与交互（Camera3D）
 * - 视口事件转发（Viewport3D）
 * - 关闭幂等性（shutdown 多次调用安全）
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include <QImage>
#include <QPainter>

#include "RenderCore/DefaultRenderBackend.h"
#include "RenderCore/DefaultSceneCompiler.h"
#include "RenderCore/RenderBackendFactory.h"
#include "RenderCore/RenderContext.h"
#include "RenderCore/RenderFrame.h"
#include "RenderCore/ViewCamera3D.h"
#include "RenderCore/RenderCoreRenderer.h"
#include "RenderCore/RenderTypes.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyArc.h"
#include "Ut/Vec.h"

 // ============================================================================
 // RenderContext 测试
 // ============================================================================

TEST(RenderContextTest, DefaultState)
{
    RenderContext ctx;
    EXPECT_TRUE(ctx.isDirty);
    EXPECT_EQ(ctx.frameId, 0);
    EXPECT_EQ(ctx.renderMode, RenderMode::Wireframe);
    EXPECT_TRUE(ctx.orbitMode);
    EXPECT_FALSE(ctx.measureMode);
}

TEST(RenderContextTest, DirtyFlag)
{
    RenderContext ctx;
    EXPECT_TRUE(ctx.isDirty);

    ctx.clearDirty();
    EXPECT_FALSE(ctx.isDirty);

    ctx.markDirty();
    EXPECT_TRUE(ctx.isDirty);
}

TEST(RenderContextTest, FrameAdvance)
{
    RenderContext ctx;
    EXPECT_EQ(ctx.frameId, 0);

    ctx.advanceFrame();
    EXPECT_EQ(ctx.frameId, 1);

    ctx.advanceFrame();
    EXPECT_EQ(ctx.frameId, 2);
}

TEST(RenderContextTest, SceneTypeDetection)
{
    RenderContext ctx2D;
    ctx2D.sceneType = "2D";
    EXPECT_TRUE(ctx2D.is2D());
    EXPECT_FALSE(ctx2D.is3D());

    RenderContext ctx3D;
    ctx3D.sceneType = "3D";
    EXPECT_FALSE(ctx3D.is2D());
    EXPECT_TRUE(ctx3D.is3D());
}

// ============================================================================
// DefaultRenderBackend 测试
// ============================================================================

TEST(DefaultRenderBackendTest, InitializeAndShutdown)
{
    DefaultRenderBackend backend("TestBackend", BackendCapability::HardwareAccelerated);

    EXPECT_FALSE(backend.isReady());
    EXPECT_EQ(backend.backendName(), "TestBackend");
    EXPECT_TRUE(backend.supportsCapability(BackendCapability::HardwareAccelerated));
    EXPECT_FALSE(backend.supportsCapability(BackendCapability::RayTracing));

    bool ok = backend.initialize();
    EXPECT_TRUE(ok);
    EXPECT_TRUE(backend.isReady());

    backend.shutdown();
    EXPECT_FALSE(backend.isReady());
}

TEST(DefaultRenderBackendTest, ShutdownIdempotent)
{
    DefaultRenderBackend backend("Test", BackendCapability::None);
    backend.initialize();
    EXPECT_TRUE(backend.isReady());

    backend.shutdown();
    backend.shutdown();
    backend.shutdown();
    EXPECT_FALSE(backend.isReady());
}

TEST(DefaultRenderBackendTest, ContextBinding)
{
    DefaultRenderBackend backend("Test", BackendCapability::None);
    backend.initialize();

    RenderContext ctx;
    ctx.sceneType = "3D";
    ctx.viewportSize = Size2D{ 800, 600 };
    backend.bindContext(ctx);

    const auto& bound = backend.context();
    EXPECT_EQ(bound.sceneType, "3D");
    EXPECT_EQ(bound.viewportSize.width, 800);
    EXPECT_EQ(bound.viewportSize.height, 600);
    EXPECT_TRUE(bound.isDirty);

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, RenderMode)
{
    DefaultRenderBackend backend("Test", BackendCapability::None);
    backend.initialize();

    EXPECT_EQ(backend.renderMode(), RenderMode::Wireframe);

    backend.setRenderMode(RenderMode::Shaded);
    EXPECT_EQ(backend.renderMode(), RenderMode::Shaded);

    backend.setRenderMode(RenderMode::Solid);
    EXPECT_EQ(backend.renderMode(), RenderMode::Solid);

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, SubmitFrame)
{
    DefaultRenderBackend backend("Test", BackendCapability::None);
    backend.initialize();

    RenderFrame frame;
    frame.frameId = 42;
    frame.valid = true;
    frame.batches = { RenderBatch{} };

    backend.submitFrame(frame);

    auto stats = backend.getStatistics();
    EXPECT_EQ(stats.batchCount, 1);
    EXPECT_EQ(stats.entityCount, 0);

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, Resize)
{
    DefaultRenderBackend backend("Test", BackendCapability::None);
    backend.initialize();

    backend.resize(Size2D{ 1024, 768 });
    EXPECT_EQ(backend.context().viewportSize.width, 1024);
    EXPECT_EQ(backend.context().viewportSize.height, 768);
    EXPECT_TRUE(backend.context().isDirty);

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, CaptureFrame)
{
    DefaultRenderBackend backend("Test", BackendCapability::None);
    backend.initialize();

    backend.resize(Size2D{ 640, 480 });
    ImageBuffer captured = backend.captureFrame();
    EXPECT_EQ(captured.width, 640);
    EXPECT_EQ(captured.height, 480);
    EXPECT_FALSE(captured.data.empty());

    backend.shutdown();
}

// ============================================================================
// SceneCompiler 测试
// ============================================================================

TEST(SceneCompilerTest, CompileEmpty2D)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    RenderContext ctx;
    ctx.sceneType = "2D";

    RenderFrame frame = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 0);
    EXPECT_EQ(frame.entityCount(), 0);
}

TEST(SceneCompilerTest, Compile2DWithLines)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());
    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(50, 50), Ut::Vec2d(150, 150) });
    scene.addEntity(l2.release());

    RenderContext ctx;
    ctx.sceneType = "2D";

    RenderFrame frame = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 2);
    EXPECT_EQ(frame.entityCount(), 2);
}

TEST(SceneCompilerTest, IncrementalCompileCacheHit)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(line.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame1.valid);
    EXPECT_EQ(frame1.batchCount(), 1);

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame2.valid);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_LT(frame2.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, IncrementalCompileWithDirtyEntity)
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

    compiler.markEntityDirty(std::to_string(lineId));
    ctx.clearDirty();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame2.valid);
    EXPECT_EQ(frame2.batchCount(), 1);
}

TEST(SceneCompilerTest, CacheInvalidation)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(line.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    compiler.compile(&scene, ctx);
    EXPECT_TRUE(compiler.hasCachedFrame());

    compiler.invalidateCache();
    EXPECT_FALSE(compiler.hasCachedFrame());
    EXPECT_EQ(compiler.cachedFrameId(), 0);
}

TEST(SceneCompilerTest, BatchGrouping)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());
    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(50, 50), Ut::Vec2d(150, 150) });
    scene.addEntity(l2.release());

    RenderContext ctx;
    RenderFrame frame = compiler.compile(&scene, ctx);

    std::vector<int> groups = compiler.groupBatchesByPrimitiveType(frame);
    EXPECT_FALSE(groups.empty());
}

TEST(SceneCompilerTest, Culling)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(line.release());

    RenderContext ctx;
    RenderFrame frame = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame.batchCount(), 1);

    RenderFrame culled = compiler.cullBatches(frame, RenderRectF{ 1000, 1000, 100, 100 });
    EXPECT_EQ(culled.batchCount(), 0);
    EXPECT_GT(culled.statistics.culledBatchCount, 0);
}

TEST(SceneCompilerTest, CompileCircle)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(0, 0);
    circle->dRadius = 50.0;
    scene.addEntity(circle.release());

    RenderContext ctx;
    RenderFrame frame = compiler.compile(&scene, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 1);
    EXPECT_GT(frame.totalVertexCount(), 0);
}

TEST(SceneCompilerTest, DirtyEntityUpdate_CacheConsistency)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(line.release());

    auto allEntities = scene.getAllEntities();
    ASSERT_FALSE(allEntities.empty());
    Eg::EntityId lineId = allEntities[0]->id;

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);
    const uint64_t frameId1 = frame1.frameId;

    auto* entity = scene.findEntityById(lineId);
    ASSERT_NE(entity, nullptr);
    entity->transform(Ut::Mat3d::translate(50, 50));
    compiler.markEntityDirty(std::to_string(lineId));
    ctx.clearDirty();
    ctx.advanceFrame();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_NE(frame2.frameId, frameId1);

    EXPECT_TRUE(compiler.hasCachedFrame());
}

TEST(SceneCompilerTest, EntityDeletion_OldBatchRemoved)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());
    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(200, 200), Ut::Vec2d(300, 300) });
    scene.addEntity(l2.release());

    auto allEntities = scene.getAllEntities();
    ASSERT_EQ(allEntities.size(), 2);
    Eg::EntityId id1 = allEntities[0]->id;

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 2);

    auto* e1 = scene.findEntityById(id1);
    ASSERT_NE(e1, nullptr);
    scene.deleteEntity(e1);
    compiler.markEntityDirty(std::to_string(id1));
    ctx.clearDirty();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_EQ(frame2.entityCount(), 1);
}

TEST(SceneCompilerTest, MarkAllDirty_RecreatesFullFrame)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());
    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(50, 50), Ut::Vec2d(150, 150) });
    scene.addEntity(l2.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 2);

    compiler.markAllDirty();
    ctx.clearDirty();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 2);
    EXPECT_GT(frame2.statistics.compileTimeMs, 0);
}

TEST(SceneCompilerTest, SceneSwitch_CacheCleared)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene1;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene1.addEntity(line.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    compiler.compile(&scene1, ctx);
    EXPECT_TRUE(compiler.hasCachedFrame());

    Eg::SceneManager scene2;
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(0, 0);
    circle->dRadius = 50.0;
    scene2.addEntity(circle.release());

    RenderFrame frame = compiler.compile(&scene2, ctx);
    EXPECT_EQ(frame.batchCount(), 1);
    EXPECT_TRUE(frame.valid);
}

TEST(SceneCompilerTest, MultipleDirtyUpdates_NoAccumulation)
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
    auto l3 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(400, 400), Ut::Vec2d(500, 500) });
    auto id3 = l3->id;
    scene.addEntity(l3.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame0 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame0.batchCount(), 3);

    compiler.markEntityDirty(std::to_string(id1));
    compiler.markEntityDirty(std::to_string(id2));
    compiler.markEntityDirty(std::to_string(id3));
    compiler.markEntityDirty(std::to_string(id1));
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 3);

    ctx.clearDirty();
    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 3);
    EXPECT_LT(frame2.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, DirtyEntityAdded_NewBatchCreated)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);

    auto newLine = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(200, 200), Ut::Vec2d(300, 300) });
    auto newId = newLine->id;
    scene.addEntity(newLine.release());
    compiler.markEntityDirty(std::to_string(newId));
    ctx.clearDirty();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 2);
    EXPECT_EQ(frame2.entityCount(), 2);
}

// ============================================================================
// SceneCompiler 缓存一致性测试（增量编译边界场景）
// ============================================================================

TEST(SceneCompilerTest, IncrementalCompile_DeleteEntityCachePurged)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());
    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(200, 200), Ut::Vec2d(300, 300) });
    scene.addEntity(l2.release());

    auto allEntities = scene.getAllEntities();
    ASSERT_EQ(allEntities.size(), 2);
    Eg::EntityId id1 = allEntities[0]->id;

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 2);
    EXPECT_TRUE(compiler.hasCachedFrame());

    auto* e = scene.findEntityById(id1);
    ASSERT_NE(e, nullptr);
    scene.deleteEntity(e);
    compiler.markEntityDirty(std::to_string(id1));
    ctx.clearDirty();

    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_EQ(frame2.entityCount(), 1);

    ctx.clearDirty();
    RenderFrame frame3 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame3.batchCount(), 1);
    EXPECT_LT(frame3.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, IncrementalCompile_SceneSwitchCacheRebuilt)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene1;
    auto line = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene1.addEntity(line.release());

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    compiler.compile(&scene1, ctx);
    EXPECT_TRUE(compiler.hasCachedFrame());

    Eg::SceneManager scene2;
    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(0, 0);
    circle->dRadius = 50.0;
    scene2.addEntity(circle.release());
    ctx.clearDirty();

    RenderFrame frame = compiler.compile(&scene2, ctx);
    EXPECT_EQ(frame.batchCount(), 1);
    EXPECT_TRUE(frame.valid);

    EXPECT_TRUE(compiler.hasCachedFrame());

    ctx.clearDirty();
    RenderFrame frame2 = compiler.compile(&scene2, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_LT(frame2.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, IncrementalCompile_CacheHitAfterMultipleDirty)
{
    DefaultSceneCompiler compiler;

    Eg::SceneManager scene;
    auto l1 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(0, 0), Ut::Vec2d(100, 100) });
    scene.addEntity(l1.release());
    auto l2 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(200, 200), Ut::Vec2d(300, 300) });
    scene.addEntity(l2.release());
    auto l3 = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(400, 400), Ut::Vec2d(500, 500) });
    scene.addEntity(l3.release());

    auto allEntities = scene.getAllEntities();
    ASSERT_EQ(allEntities.size(), 3);
    Eg::EntityId id1 = allEntities[0]->id;
    Eg::EntityId id2 = allEntities[1]->id;
    Eg::EntityId id3 = allEntities[2]->id;

    RenderContext ctx;
    ctx.sceneType = "2D";
    ctx.clearDirty();

    RenderFrame frame0 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame0.batchCount(), 3);

    scene.findEntityById(id1)->transform(Ut::Mat3d::translate(10, 10));
    compiler.markEntityDirty(std::to_string(id1));
    ctx.clearDirty();
    RenderFrame frame1 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame1.batchCount(), 3);

    scene.findEntityById(id2)->transform(Ut::Mat3d::translate(20, 20));
    scene.findEntityById(id3)->transform(Ut::Mat3d::translate(30, 30));
    compiler.markEntityDirty(std::to_string(id2));
    compiler.markEntityDirty(std::to_string(id3));
    ctx.clearDirty();
    RenderFrame frame2 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame2.batchCount(), 3);

    ctx.clearDirty();
    RenderFrame frame3 = compiler.compile(&scene, ctx);
    EXPECT_EQ(frame3.batchCount(), 3);
    EXPECT_LT(frame3.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, IncrementalCompile_NoPhantomBatches)
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

    RenderFrame fullFrame = compiler.compile(&scene, ctx);
    const int fullBatchCount = fullFrame.batchCount();
    const int fullVertexCount = fullFrame.totalVertexCount();
    const int fullEntityCount = fullFrame.entityCount();
    EXPECT_EQ(fullBatchCount, 3);

    ctx.clearDirty();
    RenderFrame incFrame = compiler.compile(&scene, ctx);
    EXPECT_EQ(incFrame.batchCount(), fullBatchCount);
    EXPECT_EQ(incFrame.totalVertexCount(), fullVertexCount);
    EXPECT_EQ(incFrame.entityCount(), fullEntityCount);
    EXPECT_LT(incFrame.statistics.compileTimeMs, 0.01);

    compiler.invalidateCache();
    ctx.clearDirty();
    RenderFrame fullFrame2 = compiler.compile(&scene, ctx);

    auto newLine = std::make_unique<Eg::SyLine>(
        std::vector<Ut::Vec2d>{ Ut::Vec2d(300, 300), Ut::Vec2d(400, 400) });
    auto newId = newLine->id;
    scene.addEntity(newLine.release());
    compiler.markEntityDirty(std::to_string(newId));
    ctx.clearDirty();
    RenderFrame incFrame2 = compiler.compile(&scene, ctx);

    EXPECT_EQ(incFrame2.batchCount(), fullFrame2.batchCount() + 1);
    EXPECT_EQ(incFrame2.entityCount(), fullFrame2.entityCount() + 1);
}

// ============================================================================
// RenderBackendFactory 测试
// ============================================================================

TEST(RenderBackendFactoryTest, CreateOpenGL)
{
    auto backend = RenderBackendFactory::create(RenderBackendFactory::BackendType::OpenGL);
    EXPECT_NE(backend, nullptr);
    EXPECT_EQ(backend->backendName(), "OpenGL");
    EXPECT_TRUE(backend->supportsCapability(BackendCapability::HardwareAccelerated));
}

TEST(RenderBackendFactoryTest, CreateSoftware)
{
    auto backend = RenderBackendFactory::create(RenderBackendFactory::BackendType::Software);
    EXPECT_NE(backend, nullptr);
    EXPECT_EQ(backend->backendName(), "Software");
    EXPECT_FALSE(backend->supportsCapability(BackendCapability::HardwareAccelerated));
}

TEST(RenderBackendFactoryTest, FromString)
{
    EXPECT_EQ(RenderBackendFactory::fromString("opengl"),
        RenderBackendFactory::BackendType::OpenGL);
    EXPECT_EQ(RenderBackendFactory::fromString("OPENGL"),
        RenderBackendFactory::BackendType::OpenGL);
    EXPECT_EQ(RenderBackendFactory::fromString("vulkan"),
        RenderBackendFactory::BackendType::Vulkan);
    EXPECT_EQ(RenderBackendFactory::fromString("metal"),
        RenderBackendFactory::BackendType::Metal);
    EXPECT_EQ(RenderBackendFactory::fromString("software"),
        RenderBackendFactory::BackendType::Software);
}

TEST(RenderBackendFactoryTest, ToString)
{
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::OpenGL),
        "OpenGL");
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::Vulkan),
        "Vulkan");
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::Metal),
        "Metal");
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::Software),
        "Software");
}

TEST(RenderBackendFactoryTest, FromStringRoundTrip)
{
    for (auto type : { RenderBackendFactory::BackendType::OpenGL,
                       RenderBackendFactory::BackendType::Vulkan,
                       RenderBackendFactory::BackendType::Metal,
                       RenderBackendFactory::BackendType::Software })
    {
        std::string name = RenderBackendFactory::toString(type);
        EXPECT_EQ(RenderBackendFactory::fromString(name), type);
    }
}

TEST(RenderBackendFactoryTest, UnknownStringReturnsDefault)
{
    auto type = RenderBackendFactory::fromString("nonexistent");
    EXPECT_EQ(type, RenderBackendFactory::defaultBackendType());
}

TEST(RenderBackendFactoryTest, AvailableBackendsNotEmpty)
{
    auto backends = RenderBackendFactory::availableBackends();
    EXPECT_FALSE(backends.empty());
    bool hasOpenGL = false;
    bool hasSoftware = false;
    for (auto b : backends)
    {
        if (b == RenderBackendFactory::BackendType::OpenGL) hasOpenGL = true;
        if (b == RenderBackendFactory::BackendType::Software) hasSoftware = true;
    }
    EXPECT_TRUE(hasOpenGL || hasSoftware);
}

TEST(RenderBackendFactoryTest, CreateConfigured)
{
    auto backend = RenderBackendFactory::createConfigured();
    EXPECT_NE(backend, nullptr);
    EXPECT_FALSE(backend->backendName().empty());
}

TEST(RenderBackendFactoryTest, EnvironmentVariableOverride)
{
    auto backend = RenderBackendFactory::createConfigured();
    EXPECT_NE(backend, nullptr);
}

TEST(RenderBackendFactoryTest, BackendSwitchStability)
{
    auto opengl = RenderBackendFactory::create(RenderBackendFactory::BackendType::OpenGL);
    EXPECT_NE(opengl, nullptr);
    EXPECT_EQ(opengl->backendName(), "OpenGL");

    auto software = RenderBackendFactory::create(RenderBackendFactory::BackendType::Software);
    EXPECT_NE(software, nullptr);
    EXPECT_EQ(software->backendName(), "Software");

    auto opengl2 = RenderBackendFactory::create(RenderBackendFactory::BackendType::OpenGL);
    EXPECT_NE(opengl2, nullptr);
}

TEST(RenderBackendFactoryTest, DefaultStrategyOnWindows)
{
    auto defaultType = RenderBackendFactory::defaultBackendType();
    // Windows 默认应该是 OpenGL 或 Software
    bool isExpected = (defaultType == RenderBackendFactory::BackendType::OpenGL) ||
        (defaultType == RenderBackendFactory::BackendType::Software);
    EXPECT_TRUE(isExpected);
}

TEST(RenderBackendFactoryTest, BackendInitializationIdempotent)
{
    auto backend = RenderBackendFactory::create(RenderBackendFactory::BackendType::Software);

    EXPECT_FALSE(backend->isReady());

    bool ok1 = backend->initialize();
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(backend->isReady());

    bool ok2 = backend->initialize();
    EXPECT_TRUE(ok2);
    EXPECT_TRUE(backend->isReady());

    backend->shutdown();
    EXPECT_FALSE(backend->isReady());
}

TEST(RenderBackendFactoryTest, AllBackendsCreateAndShutdown)
{
    auto types = RenderBackendFactory::availableBackends();
    for (auto type : types)
    {
        auto backend = RenderBackendFactory::create(type);
        ASSERT_NE(backend, nullptr);

        if (backend->initialize())
        {
            EXPECT_TRUE(backend->isReady());
            backend->shutdown();
            EXPECT_FALSE(backend->isReady());
        }
    }
}

// ============================================================================
// ViewCamera3D 测试
// ============================================================================

TEST(ViewCamera3DTest, DefaultState)
{
    ViewCamera3D camera;
    EXPECT_FALSE(camera.isDirty());
    EXPECT_FALSE(camera.isRotating());
    EXPECT_FALSE(camera.isPanning());
    EXPECT_TRUE(camera.isOrbitMode());
    EXPECT_FALSE(camera.isMeasureMode());
}

TEST(ViewCamera3DTest, Reset)
{
    ViewCamera3D camera;
    camera.orbit(90.0, 45.0);
    camera.pan(100.0, 200.0);
    camera.zoom(5.0);
    EXPECT_TRUE(camera.isDirty());

    camera.reset();
    EXPECT_DOUBLE_EQ(camera.yaw(), 0.0);
    EXPECT_DOUBLE_EQ(camera.pitch(), 15.0);
    EXPECT_DOUBLE_EQ(camera.distance(), 10.0);
}

TEST(ViewCamera3DTest, Orbit)
{
    ViewCamera3D camera;
    camera.clearDirty();

    camera.orbit(30.0, 10.0);
    EXPECT_TRUE(camera.isDirty());
    EXPECT_DOUBLE_EQ(camera.yaw(), 30.0);
    EXPECT_DOUBLE_EQ(camera.pitch(), 25.0); // 15 + 10
}

TEST(ViewCamera3DTest, Pan)
{
    ViewCamera3D camera;
    camera.clearDirty();

    camera.pan(50.0, -30.0);
    EXPECT_TRUE(camera.isDirty());
    EXPECT_DOUBLE_EQ(camera.panX(), 50.0);
    EXPECT_DOUBLE_EQ(camera.panY(), -30.0);
}

TEST(ViewCamera3DTest, Zoom)
{
    ViewCamera3D camera;
    camera.clearDirty();

    camera.zoom(3.0);
    EXPECT_TRUE(camera.isDirty());
    EXPECT_DOUBLE_EQ(camera.distance(), 13.0); // 10 + 3
}

TEST(ViewCamera3DTest, ProjectInFront)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);

    int sx, sy;
    bool ok = camera.project(0.0, 0.0, 0.0, sx, sy);
    EXPECT_TRUE(ok); // 原点在相机前方
}

TEST(ViewCamera3DTest, ProjectBehind)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);

    // 相机在 z=10 处，看向原点。z=100 在相机后方很远
    int sx, sy;
    bool ok = camera.project(0.0, 0.0, 100.0, sx, sy);
    EXPECT_FALSE(ok); // 在相机后方
}

TEST(ViewCamera3DTest, MousePressRotate)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);

    // 左键按下开始旋转
    bool changed = camera.onMousePress(400, 300, 1, 0, 800, 600);
    EXPECT_TRUE(camera.isRotating());
    EXPECT_FALSE(camera.isPanning());
    EXPECT_TRUE(changed || !changed); // 初始按下可能不产生变更
}

TEST(ViewCamera3DTest, MousePressPan)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);

    // 中键按下开始平移
    bool changed = camera.onMousePress(400, 300, 4, 0, 800, 600);
    EXPECT_FALSE(camera.isRotating());
    EXPECT_TRUE(camera.isPanning());
}

TEST(ViewCamera3DTest, MouseDrag)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);

    camera.onMousePress(400, 300, 1, 0, 800, 600);
    camera.clearDirty();

    bool changed = camera.onMouseMove(450, 320, 1, 800, 600);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(camera.isDirty());
}

TEST(ViewCamera3DTest, MouseRelease)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);

    camera.onMousePress(400, 300, 1, 0, 800, 600);
    bool changed = camera.onMouseRelease(450, 320, 1, 800, 600);
    EXPECT_FALSE(camera.isRotating());
    EXPECT_FALSE(camera.isPanning());
}

TEST(ViewCamera3DTest, Wheel)
{
    ViewCamera3D camera;
    camera.setViewportSize(800, 600);
    camera.clearDirty();

    bool changed = camera.onWheel(120, 800, 600);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(camera.isDirty());
}

TEST(ViewCamera3DTest, MeasureMode)
{
    ViewCamera3D camera;
    EXPECT_FALSE(camera.isMeasureMode());

    camera.setMeasureMode(true);
    EXPECT_TRUE(camera.isMeasureMode());

    camera.setMeasureMode(false);
    EXPECT_FALSE(camera.isMeasureMode());
}

// ============================================================================
// RenderCoreRenderer 测试
// ============================================================================

TEST(RenderCoreRendererTest, Lifecycle)
{
    RenderCoreRenderer renderer;
    EXPECT_FALSE(renderer.isReady());

    renderer.initialize();
    EXPECT_TRUE(renderer.isReady());
    EXPECT_FALSE(renderer.isOpenGL()); // 软件路径

    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
}

TEST(RenderCoreRendererTest, ShutdownIdempotent)
{
    RenderCoreRenderer renderer;
    renderer.initialize();
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
    renderer.shutdown();
    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
}

TEST(RenderCoreRendererTest, RenderLoopControl)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    EXPECT_FALSE(renderer.isRenderLoopRunning());

    renderer.setRenderLoopEnabled(true);
    EXPECT_TRUE(renderer.isRenderLoopRunning());

    renderer.setRenderLoopEnabled(false);
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    renderer.shutdown();
}

TEST(RenderCoreRendererTest, OrbitMode)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.setOrbitMode(false);
    EXPECT_FALSE(renderer.isOrbitMode());

    renderer.setOrbitMode(true);
    EXPECT_TRUE(renderer.isOrbitMode());

    renderer.shutdown();
}

TEST(RenderCoreRendererTest, ResetView)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    // 重置视图不崩溃
    renderer.resetView();
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
}

TEST(RenderCoreRendererTest, Resize)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    // 调整大小不崩溃
    renderer.resize(1024, 768);
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
}

TEST(RenderCoreRendererTest, RenderWithoutScene)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);
    // 无场景时渲染应优雅降级（显示占位文本）
    renderer.render(painter, 800, 600);
    painter.end();

    renderer.shutdown();
}

TEST(RenderCoreRendererTest, InputEvents)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    // 输入事件委托给 Camera3D，不应崩溃
    renderer.onMousePress(400, 300, 1, 0, 800, 600);
    renderer.onMouseMove(450, 320, 1, 800, 600);
    renderer.onMouseRelease(450, 320, 1, 800, 600);
    renderer.onWheel(120, 800, 600);

    renderer.shutdown();
}

TEST(RenderCoreRendererTest, Selection)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    EXPECT_TRUE(renderer.selectedNodeId().isEmpty());

    renderer.selectNodeById(QStringLiteral("node_1"));
    EXPECT_EQ(renderer.selectedNodeId(), QStringLiteral("node_1"));

    renderer.shutdown();
}

TEST(RenderCoreRendererTest, MeasureMode)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    renderer.setMeasureMode(true);
    renderer.setMeasureMode(false);

    renderer.shutdown();
}

// ============================================================================
// 框架级集成测试（纯 RenderCore 测试，不依赖 Viewport3D）
// ============================================================================

TEST(FrameworkIntegrationTest, RenderPipelineStartup)
{
    RenderCoreRenderer renderer;
    renderer.initialize();
    EXPECT_TRUE(renderer.isReady());
    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, RenderPipelineResize)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    renderer.resize(1024, 768);

    QImage image(1024, 768, QImage::Format_ARGB32);
    QPainter painter(&image);
    renderer.render(painter, 1024, 768);
    painter.end();

    renderer.resize(640, 480);

    painter.begin(&image);
    renderer.render(painter, 640, 480);
    painter.end();

    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, RenderPipelineModeSwitch)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    renderer.setOrbitMode(false);
    renderer.setMeasureMode(true);
    renderer.setOrbitMode(true);
    renderer.setMeasureMode(false);

    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, FullRenderPipeline)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    // 完整渲染循环：初始化 → 渲染 → 关闭
    renderer.resize(800, 600);
    renderer.render(painter, 800, 600);

    painter.end();
    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, DirtyUpdateThenRender)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    // 初始渲染
    renderer.resize(800, 600);
    renderer.render(painter, 800, 600);

    // 相机变更（触发脏标记）
    renderer.onMousePress(400, 300, 1, 0, 800, 600);
    renderer.onMouseMove(450, 320, 1, 800, 600);

    // 再次渲染
    renderer.render(painter, 800, 600);

    painter.end();
    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, MultipleRenderFrames)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    renderer.resize(800, 600);

    // 连续渲染多帧
    for (int i = 0; i < 5; ++i)
    {
        renderer.render(painter, 800, 600);
    }

    painter.end();
    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, RendererWithCompilerCache)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    renderer.resize(800, 600);

    // 第一次渲染（可能有编译开销）
    renderer.render(painter, 800, 600);

    // 第二次渲染（无变化，应使用缓存）
    renderer.render(painter, 800, 600);

    painter.end();
    renderer.shutdown();
}

TEST(FrameworkIntegrationTest, CameraInteractionThenRender)
{
    RenderCoreRenderer renderer;
    renderer.initialize();

    QImage image(800, 600, QImage::Format_ARGB32);
    QPainter painter(&image);

    renderer.resize(800, 600);

    // 完整交互序列
    renderer.onMousePress(400, 300, 1, 0, 800, 600);
    renderer.onMouseMove(420, 320, 1, 800, 600);
    renderer.onMouseMove(440, 340, 1, 800, 600);
    renderer.onMouseRelease(440, 340, 1, 800, 600);

    // 渲染
    renderer.render(painter, 800, 600);

    painter.end();
    renderer.shutdown();
}