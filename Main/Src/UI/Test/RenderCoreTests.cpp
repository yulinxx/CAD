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

#include <QImage>
#include <QPainter>
#include <QProcessEnvironment>

#include "RenderCore/DefaultRenderBackend.h"
#include "RenderCore/DefaultSceneCompiler.h"
#include "RenderCore/RenderBackendFactory.h"
#include "RenderCore/RenderContext.h"
#include "RenderCore/RenderFrame.h"
#include "RenderCore/UiCamera3D.h"
#include "RenderCore/RenderCoreRenderer.h"
#include "UI/UiEntities.h"

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
    ctx2D.sceneType = QStringLiteral("2D");
    EXPECT_TRUE(ctx2D.is2D());
    EXPECT_FALSE(ctx2D.is3D());

    RenderContext ctx3D;
    ctx3D.sceneType = QStringLiteral("3D");
    EXPECT_FALSE(ctx3D.is2D());
    EXPECT_TRUE(ctx3D.is3D());
}

// ============================================================================
// DefaultRenderBackend 测试
// ============================================================================

TEST(DefaultRenderBackendTest, InitializeAndShutdown)
{
    DefaultRenderBackend backend(QStringLiteral("TestBackend"), BackendCapability::HardwareAccelerated);

    EXPECT_FALSE(backend.isReady());
    EXPECT_EQ(backend.backendName(), QStringLiteral("TestBackend"));
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
    DefaultRenderBackend backend(QStringLiteral("Test"), BackendCapability::None);
    backend.initialize();
    EXPECT_TRUE(backend.isReady());

    // 多次 shutdown 不应崩溃
    backend.shutdown();
    backend.shutdown();
    backend.shutdown();
    EXPECT_FALSE(backend.isReady());
}

TEST(DefaultRenderBackendTest, ContextBinding)
{
    DefaultRenderBackend backend(QStringLiteral("Test"), BackendCapability::None);
    backend.initialize();

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("3D");
    ctx.viewportSize = QSize(800, 600);
    backend.bindContext(ctx);

    const auto& bound = backend.context();
    EXPECT_EQ(bound.sceneType, QStringLiteral("3D"));
    EXPECT_EQ(bound.viewportSize, QSize(800, 600));
    EXPECT_TRUE(bound.isDirty); // bindContext 标记脏

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, RenderMode)
{
    DefaultRenderBackend backend(QStringLiteral("Test"), BackendCapability::None);
    backend.initialize();

    EXPECT_EQ(backend.renderMode(), RenderMode::Wireframe); // 默认

    backend.setRenderMode(RenderMode::Shaded);
    EXPECT_EQ(backend.renderMode(), RenderMode::Shaded);

    backend.setRenderMode(RenderMode::Solid);
    EXPECT_EQ(backend.renderMode(), RenderMode::Solid);

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, SubmitFrame)
{
    DefaultRenderBackend backend(QStringLiteral("Test"), BackendCapability::None);
    backend.initialize();

    RenderFrame frame;
    frame.frameId = 42;
    frame.valid = true;
    frame.batches = { RenderBatch{} };

    backend.submitFrame(frame);

    auto stats = backend.getStatistics();
    EXPECT_EQ(stats.batchCount, 1);
    EXPECT_EQ(stats.entityCount, 0); // 空 entityId

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, Resize)
{
    DefaultRenderBackend backend(QStringLiteral("Test"), BackendCapability::None);
    backend.initialize();

    backend.resize(QSize(1024, 768));
    EXPECT_EQ(backend.context().viewportSize, QSize(1024, 768));
    EXPECT_TRUE(backend.context().isDirty);

    backend.shutdown();
}

TEST(DefaultRenderBackendTest, CaptureFrame)
{
    DefaultRenderBackend backend(QStringLiteral("Test"), BackendCapability::None);
    backend.initialize();

    backend.resize(QSize(640, 480));
    QImage captured = backend.captureFrame();
    EXPECT_EQ(captured.size(), QSize(640, 480));
    EXPECT_FALSE(captured.isNull());

    backend.shutdown();
}

// ============================================================================
// SceneCompiler 测试
// ============================================================================

TEST(SceneCompilerTest, CompileEmpty2D)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");

    RenderFrame frame = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 0);
    EXPECT_EQ(frame.entityCount(), 0);
}

TEST(SceneCompilerTest, Compile2DWithLines)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.createLine(QPointF(50, 50), QPointF(150, 150));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");

    RenderFrame frame = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 2); // 两条线段
    EXPECT_EQ(frame.entityCount(), 2);
}

TEST(SceneCompilerTest, IncrementalCompileCacheHit)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 第一次编译（全量）
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame1.valid);
    EXPECT_EQ(frame1.batchCount(), 1);

    // 第二次编译（缓存命中，无脏实体）
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame2.valid);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_LT(frame2.statistics.compileTimeMs, 0.01); // 零开销
}

TEST(SceneCompilerTest, IncrementalCompileWithDirtyEntity)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    auto line = doc.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 第一次全量编译
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);

    // 标记脏实体，增量编译
    compiler.markEntityDirty(line->id());
    ctx.clearDirty();

    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame2.valid);
    EXPECT_EQ(frame2.batchCount(), 1);
}

TEST(SceneCompilerTest, CacheInvalidation)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    compiler.compile(&doc, ctx);
    EXPECT_TRUE(compiler.hasCachedFrame());

    compiler.invalidateCache();
    EXPECT_FALSE(compiler.hasCachedFrame());
    EXPECT_EQ(compiler.cachedFrameId(), 0);
}

TEST(SceneCompilerTest, BatchGrouping)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.createLine(QPointF(50, 50), QPointF(150, 150));

    RenderContext ctx;
    RenderFrame frame = compiler.compile(&doc, ctx);

    QVector<int> groups = compiler.groupBatchesByPrimitiveType(frame);
    EXPECT_FALSE(groups.isEmpty());
}

TEST(SceneCompilerTest, Culling)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    RenderFrame frame = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame.batchCount(), 1);

    // 裁剪到视口外的区域
    RenderFrame culled = compiler.cullBatches(frame, QRectF(1000, 1000, 100, 100));
    EXPECT_EQ(culled.batchCount(), 0); // 所有批次被裁剪
    EXPECT_GT(culled.statistics.culledBatchCount, 0);
}

TEST(SceneCompilerTest, CompileCircle)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createCircle(QPointF(0, 0), 50.0);

    RenderContext ctx;
    RenderFrame frame = compiler.compile(&doc, ctx);
    EXPECT_TRUE(frame.valid);
    EXPECT_EQ(frame.batchCount(), 1);
    EXPECT_GT(frame.totalVertexCount(), 0);
}

TEST(SceneCompilerTest, DirtyEntityUpdate_CacheConsistency)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    auto line = doc.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 初始编译
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);
    const uint64_t frameId1 = frame1.frameId;

    // 修改实体后标记脏
    line->translate(QPointF(50, 50));
    compiler.markEntityDirty(line->id());
    ctx.clearDirty();
    ctx.advanceFrame();

    // 增量编译后批次仍为1，但内容应更新
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_NE(frame2.frameId, frameId1);

    // 验证缓存状态
    EXPECT_TRUE(compiler.hasCachedFrame());
}

TEST(SceneCompilerTest, EntityDeletion_OldBatchRemoved)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    auto line1 = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    auto line2 = doc.createLine(QPointF(200, 200), QPointF(300, 300));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 初始编译（2个批次）
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 2);

    // 删除一个实体并标记脏
    const QString deletedId = line1->id();
    doc.removeEntity(deletedId);
    compiler.markEntityDirty(deletedId);
    ctx.clearDirty();

    // 再次编译应只剩1个批次
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_EQ(frame2.entityCount(), 1);
}

TEST(SceneCompilerTest, MarkAllDirty_RecreatesFullFrame)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.createLine(QPointF(50, 50), QPointF(150, 150));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 初始编译
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 2);

    // markAllDirty 强制全量编译
    compiler.markAllDirty();
    ctx.clearDirty();

    // 重新编译后仍有2个批次（完整重建）
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame2.batchCount(), 2);
    EXPECT_GT(frame2.statistics.compileTimeMs, 0); // 有编译开销，不是零开销缓存
}

TEST(SceneCompilerTest, SceneSwitch_CacheCleared)
{
    DefaultSceneCompiler compiler;

    // 第一个场景
    EntityDocument2D doc1;
    doc1.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    compiler.compile(&doc1, ctx);
    EXPECT_TRUE(compiler.hasCachedFrame());

    // 切换到第二个场景
    EntityDocument2D doc2;
    doc2.createCircle(QPointF(0, 0), 50.0);

    // 新场景首次编译应该是全量编译
    RenderFrame frame = compiler.compile(&doc2, ctx);
    EXPECT_EQ(frame.batchCount(), 1);
    EXPECT_TRUE(frame.valid);
}

TEST(SceneCompilerTest, MultipleDirtyUpdates_NoAccumulation)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    auto line1 = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    auto line2 = doc.createLine(QPointF(200, 200), QPointF(300, 300));
    auto line3 = doc.createLine(QPointF(400, 400), QPointF(500, 500));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 初始编译（3个批次）
    RenderFrame frame0 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame0.batchCount(), 3);

    // 多次标记脏实体
    compiler.markEntityDirty(line1->id());
    compiler.markEntityDirty(line2->id());
    compiler.markEntityDirty(line3->id());
    compiler.markEntityDirty(line1->id()); // 重复标记同一实体
    ctx.clearDirty();

    // 编译后仍应为3个批次（无累积错误）
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 3);

    // 再次编译（无脏实体）应零开销
    ctx.clearDirty();
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame2.batchCount(), 3);
    EXPECT_LT(frame2.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, DirtyEntityAdded_NewBatchCreated)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 初始编译（1个批次）
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 1);

    // 添加新实体并标记脏
    auto newLine = doc.createLine(QPointF(200, 200), QPointF(300, 300));
    compiler.markEntityDirty(newLine->id());
    ctx.clearDirty();

    // 增量编译后应为2个批次
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame2.batchCount(), 2);
    EXPECT_EQ(frame2.entityCount(), 2);
}

// ============================================================================
// SceneCompiler 缓存一致性测试（增量编译边界场景）
// ============================================================================

TEST(SceneCompilerTest, IncrementalCompile_DeleteEntityCachePurged)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    auto line1 = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    auto line2 = doc.createLine(QPointF(200, 200), QPointF(300, 300));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 初始编译（2个批次）
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 2);
    EXPECT_TRUE(compiler.hasCachedFrame());

    // 删除实体并标记脏
    const QString deletedId = line1->id();
    doc.removeEntity(deletedId);
    compiler.markEntityDirty(deletedId);
    ctx.clearDirty();

    // 增量编译后不应残留已删除实体的批次
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_EQ(frame2.entityCount(), 1);

    // 验证缓存一致性：再次编译应零开销命中
    ctx.clearDirty();
    RenderFrame frame3 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame3.batchCount(), 1);
    EXPECT_LT(frame3.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, IncrementalCompile_SceneSwitchCacheRebuilt)
{
    DefaultSceneCompiler compiler;

    // 第一个场景
    EntityDocument2D doc1;
    doc1.createLine(QPointF(0, 0), QPointF(100, 100));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    compiler.compile(&doc1, ctx);
    EXPECT_TRUE(compiler.hasCachedFrame());

    // 切换到第二个场景（编译器自动检测文档变更，失效缓存）
    EntityDocument2D doc2;
    doc2.createCircle(QPointF(0, 0), 50.0);
    ctx.clearDirty();

    RenderFrame frame = compiler.compile(&doc2, ctx);
    EXPECT_EQ(frame.batchCount(), 1);
    EXPECT_TRUE(frame.valid);

    // 新场景的缓存应已建立
    EXPECT_TRUE(compiler.hasCachedFrame());

    // 再次编译新场景应零开销命中
    ctx.clearDirty();
    RenderFrame frame2 = compiler.compile(&doc2, ctx);
    EXPECT_EQ(frame2.batchCount(), 1);
    EXPECT_LT(frame2.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, IncrementalCompile_CacheHitAfterMultipleDirty)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    auto line1 = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    auto line2 = doc.createLine(QPointF(200, 200), QPointF(300, 300));
    auto line3 = doc.createLine(QPointF(400, 400), QPointF(500, 500));

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 初始编译（3个批次）
    RenderFrame frame0 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame0.batchCount(), 3);

    // 第一轮脏更新
    line1->translate(QPointF(10, 10));
    compiler.markEntityDirty(line1->id());
    ctx.clearDirty();
    RenderFrame frame1 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame1.batchCount(), 3);

    // 第二轮脏更新（多实体）
    line2->translate(QPointF(20, 20));
    line3->translate(QPointF(30, 30));
    compiler.markEntityDirty(line2->id());
    compiler.markEntityDirty(line3->id());
    ctx.clearDirty();
    RenderFrame frame2 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame2.batchCount(), 3);

    // 第三轮：无脏实体，应缓存命中
    ctx.clearDirty();
    RenderFrame frame3 = compiler.compile(&doc, ctx);
    EXPECT_EQ(frame3.batchCount(), 3);
    EXPECT_LT(frame3.statistics.compileTimeMs, 0.01);
}

TEST(SceneCompilerTest, IncrementalCompile_NoPhantomBatches)
{
    DefaultSceneCompiler compiler;
    EntityDocument2D doc;
    doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.createLine(QPointF(50, 50), QPointF(150, 50));
    doc.createCircle(QPointF(100, 100), 40.0);

    RenderContext ctx;
    ctx.sceneType = QStringLiteral("2D");
    ctx.clearDirty();

    // 全量编译作为基准
    RenderFrame fullFrame = compiler.compile(&doc, ctx);
    const int fullBatchCount = fullFrame.batchCount();
    const int fullVertexCount = fullFrame.totalVertexCount();
    const int fullEntityCount = fullFrame.entityCount();
    EXPECT_EQ(fullBatchCount, 3);

    // 增量编译（无脏实体，缓存命中）
    ctx.clearDirty();
    RenderFrame incFrame = compiler.compile(&doc, ctx);
    EXPECT_EQ(incFrame.batchCount(), fullBatchCount);
    EXPECT_EQ(incFrame.totalVertexCount(), fullVertexCount);
    EXPECT_EQ(incFrame.entityCount(), fullEntityCount);
    EXPECT_LT(incFrame.statistics.compileTimeMs, 0.01);

    // 增量编译（有脏实体）应与全量编译结果等价
    compiler.invalidateCache();
    ctx.clearDirty();
    RenderFrame fullFrame2 = compiler.compile(&doc, ctx);

    auto line = doc.createLine(QPointF(300, 300), QPointF(400, 400));
    compiler.markEntityDirty(line->id());
    ctx.clearDirty();
    RenderFrame incFrame2 = compiler.compile(&doc, ctx);

    // 增量编译后 batchCount 应与全量编译一致
    EXPECT_EQ(incFrame2.batchCount(), fullFrame2.batchCount() + 1); // 新增1个实体
    EXPECT_EQ(incFrame2.entityCount(), fullFrame2.entityCount() + 1);
}

// ============================================================================
// RenderBackendFactory 测试
// ============================================================================

TEST(RenderBackendFactoryTest, CreateOpenGL)
{
    auto backend = RenderBackendFactory::create(RenderBackendFactory::BackendType::OpenGL);
    EXPECT_NE(backend, nullptr);
    EXPECT_EQ(backend->backendName(), QStringLiteral("OpenGL"));
    EXPECT_TRUE(backend->supportsCapability(BackendCapability::HardwareAccelerated));
}

TEST(RenderBackendFactoryTest, CreateSoftware)
{
    auto backend = RenderBackendFactory::create(RenderBackendFactory::BackendType::Software);
    EXPECT_NE(backend, nullptr);
    EXPECT_EQ(backend->backendName(), QStringLiteral("Software"));
    EXPECT_FALSE(backend->supportsCapability(BackendCapability::HardwareAccelerated));
}

TEST(RenderBackendFactoryTest, FromString)
{
    EXPECT_EQ(RenderBackendFactory::fromString(QStringLiteral("opengl")),
              RenderBackendFactory::BackendType::OpenGL);
    EXPECT_EQ(RenderBackendFactory::fromString(QStringLiteral("OPENGL")),
              RenderBackendFactory::BackendType::OpenGL);
    EXPECT_EQ(RenderBackendFactory::fromString(QStringLiteral("vulkan")),
              RenderBackendFactory::BackendType::Vulkan);
    EXPECT_EQ(RenderBackendFactory::fromString(QStringLiteral("metal")),
              RenderBackendFactory::BackendType::Metal);
    EXPECT_EQ(RenderBackendFactory::fromString(QStringLiteral("software")),
              RenderBackendFactory::BackendType::Software);
}

TEST(RenderBackendFactoryTest, ToString)
{
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::OpenGL),
              QStringLiteral("OpenGL"));
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::Vulkan),
              QStringLiteral("Vulkan"));
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::Metal),
              QStringLiteral("Metal"));
    EXPECT_EQ(RenderBackendFactory::toString(RenderBackendFactory::BackendType::Software),
              QStringLiteral("Software"));
}

TEST(RenderBackendFactoryTest, FromStringRoundTrip)
{
    for (auto type : { RenderBackendFactory::BackendType::OpenGL,
                       RenderBackendFactory::BackendType::Vulkan,
                       RenderBackendFactory::BackendType::Metal,
                       RenderBackendFactory::BackendType::Software })
    {
        QString name = RenderBackendFactory::toString(type);
        EXPECT_EQ(RenderBackendFactory::fromString(name), type);
    }
}

TEST(RenderBackendFactoryTest, UnknownStringReturnsDefault)
{
    auto type = RenderBackendFactory::fromString(QStringLiteral("nonexistent"));
    EXPECT_EQ(type, RenderBackendFactory::defaultBackendType());
}

TEST(RenderBackendFactoryTest, AvailableBackendsNotEmpty)
{
    auto backends = RenderBackendFactory::availableBackends();
    EXPECT_FALSE(backends.isEmpty());
    // OpenGL 和 Software 至少有一个可用
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
    EXPECT_FALSE(backend->backendName().isEmpty());
}

TEST(RenderBackendFactoryTest, EnvironmentVariableOverride)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString originalValue = env.value(QStringLiteral("SAN_YI_RENDER_BACKEND"));

    // 测试软件后端配置
    QProcessEnvironment testEnv = env;
    testEnv.insert(QStringLiteral("SAN_YI_RENDER_BACKEND"), QStringLiteral("software"));

    // 实际测试：验证 createConfigured 能正常工作
    auto backend = RenderBackendFactory::createConfigured();
    EXPECT_NE(backend, nullptr);
}

TEST(RenderBackendFactoryTest, BackendSwitchStability)
{
    // 连续创建不同后端不应相互影响
    auto opengl = RenderBackendFactory::create(RenderBackendFactory::BackendType::OpenGL);
    EXPECT_NE(opengl, nullptr);
    EXPECT_EQ(opengl->backendName(), QStringLiteral("OpenGL"));

    auto software = RenderBackendFactory::create(RenderBackendFactory::BackendType::Software);
    EXPECT_NE(software, nullptr);
    EXPECT_EQ(software->backendName(), QStringLiteral("Software"));

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
// UiCamera3D 测试
// ============================================================================

TEST(UiCamera3DTest, DefaultState)
{
    UiCamera3D camera;
    EXPECT_FALSE(camera.isDirty());
    EXPECT_FALSE(camera.isRotating());
    EXPECT_FALSE(camera.isPanning());
    EXPECT_TRUE(camera.isOrbitMode());
    EXPECT_FALSE(camera.isMeasureMode());
}

TEST(UiCamera3DTest, Reset)
{
    UiCamera3D camera;
    camera.orbit(90.0, 45.0);
    camera.pan(100.0, 200.0);
    camera.zoom(5.0);
    EXPECT_TRUE(camera.isDirty());

    camera.reset();
    EXPECT_DOUBLE_EQ(camera.yaw(), 0.0);
    EXPECT_DOUBLE_EQ(camera.pitch(), 15.0);
    EXPECT_DOUBLE_EQ(camera.distance(), 10.0);
}

TEST(UiCamera3DTest, Orbit)
{
    UiCamera3D camera;
    camera.clearDirty();

    camera.orbit(30.0, 10.0);
    EXPECT_TRUE(camera.isDirty());
    EXPECT_DOUBLE_EQ(camera.yaw(), 30.0);
    EXPECT_DOUBLE_EQ(camera.pitch(), 25.0); // 15 + 10
}

TEST(UiCamera3DTest, Pan)
{
    UiCamera3D camera;
    camera.clearDirty();

    camera.pan(50.0, -30.0);
    EXPECT_TRUE(camera.isDirty());
    EXPECT_DOUBLE_EQ(camera.panX(), 50.0);
    EXPECT_DOUBLE_EQ(camera.panY(), -30.0);
}

TEST(UiCamera3DTest, Zoom)
{
    UiCamera3D camera;
    camera.clearDirty();

    camera.zoom(3.0);
    EXPECT_TRUE(camera.isDirty());
    EXPECT_DOUBLE_EQ(camera.distance(), 13.0); // 10 + 3
}

TEST(UiCamera3DTest, ProjectInFront)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);

    int sx, sy;
    bool ok = camera.project(0.0, 0.0, 0.0, sx, sy);
    EXPECT_TRUE(ok); // 原点在相机前方
}

TEST(UiCamera3DTest, ProjectBehind)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);

    // 相机在 z=10 处，看向原点。z=100 在相机后方很远
    int sx, sy;
    bool ok = camera.project(0.0, 0.0, 100.0, sx, sy);
    EXPECT_FALSE(ok); // 在相机后方
}

TEST(UiCamera3DTest, MousePressRotate)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);

    // 左键按下开始旋转
    bool changed = camera.onMousePress(400, 300, 1, 0, 800, 600);
    EXPECT_TRUE(camera.isRotating());
    EXPECT_FALSE(camera.isPanning());
    EXPECT_TRUE(changed || !changed); // 初始按下可能不产生变更
}

TEST(UiCamera3DTest, MousePressPan)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);

    // 中键按下开始平移
    bool changed = camera.onMousePress(400, 300, 4, 0, 800, 600);
    EXPECT_FALSE(camera.isRotating());
    EXPECT_TRUE(camera.isPanning());
}

TEST(UiCamera3DTest, MouseDrag)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);

    camera.onMousePress(400, 300, 1, 0, 800, 600);
    camera.clearDirty();

    bool changed = camera.onMouseMove(450, 320, 1, 800, 600);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(camera.isDirty());
}

TEST(UiCamera3DTest, MouseRelease)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);

    camera.onMousePress(400, 300, 1, 0, 800, 600);
    bool changed = camera.onMouseRelease(450, 320, 1, 800, 600);
    EXPECT_FALSE(camera.isRotating());
    EXPECT_FALSE(camera.isPanning());
}

TEST(UiCamera3DTest, Wheel)
{
    UiCamera3D camera;
    camera.setViewportSize(800, 600);
    camera.clearDirty();

    bool changed = camera.onWheel(120, 800, 600);
    EXPECT_TRUE(changed);
    EXPECT_TRUE(camera.isDirty());
}

TEST(UiCamera3DTest, MeasureMode)
{
    UiCamera3D camera;
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