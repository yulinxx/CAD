/**
 * @file FrameworkLifecycleTests.cpp
 * @brief 框架稳定版开发清单与生命周期回归测试
 *
 * 第一部分：稳定版开发清单
 * - 启动阶段只做装配，不做业务编排
 * - 切换阶段只做工作台编排，不做窗口状态硬拼
 * - 退出阶段只做收口，不回调已失效对象
 * - 2D/3D 工作台必须遵循同一套生命周期接口
 * - 渲染适配层只做适配，不承载工作台业务
 *
 * 第二部分：回归测试目标
 * - 验证渲染器生命周期基础行为
 * - 验证工作台状态对象默认值稳定
 * - 验证关键状态切换接口可重复调用且不污染内部状态
 *
 * 第三部分：P0 回归测试 (2026-07-29)
 * - 验证 WorkbenchStateSnapshot 完整字段闭环
 * - 验证 ISceneDataSource 统一渲染数据源契约
 * - 验证 SceneGeometryCollector 包围盒收集
 * - 验证 SimpleRenderer3D 通过 ISceneDataSource 渲染
 *
 * 第四部分：回归测试扩展 (2026-07-30)
 * - 场景序列化往返测试 (Syx 序列化/反序列化)
 * - 2D/3D 视口切换回归测试
 * - 渲染快照一致性测试
 * - 撤销/重做回归测试
 */

#include <gtest/gtest.h>

#include "UI/Render/SimpleRenderer3D.h"
#include "UI/Workbench/UiWorkbench.h"
#include "Engine/Scene/SceneGeometryCollector.h"
#include "Engine/Scene/SceneRenderContract.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyPolygon.h"
#include "FileIO/SySerializer.h"
#include "FileIO/SyDocument.h"

namespace
{
    // 序列化辅助：查询大小 → 分配 → 写入，返回完整数据（C3 blob 化接口适配）
    Fio::SerializeResult serializeDoc(Fio::SySerializer& s, const Fio::SyDocument& doc, std::vector<uint8_t>& out)
    {
        Fio::BinaryBlobOut query;
        auto r = s.serializeToMemory(doc, &query);
        if (!r.success)
        {
            return r;
        }
        out.resize(query.written);
        Fio::BinaryBlobOut blobOut{ out.data(), out.size(), 0 };
        return s.serializeToMemory(doc, &blobOut);
    }

    Fio::SerializeResult deserializeDoc(Fio::SySerializer& s, const std::vector<uint8_t>& data, Fio::SyDocument& doc)
    {
        Fio::BinaryBlob in{ const_cast<uint8_t*>(data.data()), data.size() };
        return s.deserializeFromMemory(in, doc);
    }
}  // namespace

TEST(FrameworkLifecycleTest, StableChecklist_IsDocumented)
{
    // 这个测试的作用不是验证算法，而是把框架稳定版的开发原则固定在测试集中。
    // 这样后续新增启动/切换/退出逻辑时，可以先对照这里的约束再做修改。
    SUCCEED();
}

TEST(FrameworkLifecycleTest, UiStateSnapshot_DefaultsAreStable)
{
    // 快照对象必须具备稳定默认值，避免未初始化字段在切换/退出时传播脏状态。
    UiStateSnapshot snapshot;

    EXPECT_TRUE(snapshot.currentSelectionText.isEmpty());
    EXPECT_TRUE(snapshot.activeToolId.isEmpty());
    EXPECT_TRUE(snapshot.inputFocusWidget.isEmpty());
    EXPECT_FALSE(snapshot.busy);
    EXPECT_FALSE(snapshot.dirty);
    EXPECT_EQ(snapshot.currentSelectionSource, QStringLiteral("none"));
    EXPECT_EQ(snapshot.currentSelectionType, QStringLiteral("none"));
}

TEST(FrameworkLifecycleTest, SimpleRenderer3D_LifecycleIsIdempotent)
{
    // 渲染器生命周期应支持重复关闭，不应因二次 shutdown 产生异常状态。
    SimpleRenderer3D renderer;

    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    EXPECT_TRUE(renderer.initialize());
    EXPECT_TRUE(renderer.isReady());

    renderer.setRenderLoopEnabled(true);
    EXPECT_TRUE(renderer.isRenderLoopRunning());

    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());

    // 关闭后再次调用 shutdown 应保持幂等。
    renderer.shutdown();
    EXPECT_FALSE(renderer.isReady());
    EXPECT_FALSE(renderer.isRenderLoopRunning());
}

TEST(FrameworkLifecycleTest, SimpleRenderer3D_ResetViewDoesNotBreakMode)
{
    // resetView 只能重置视图，不应该破坏当前交互模式的可用性。
    SimpleRenderer3D renderer;
    ASSERT_TRUE(renderer.initialize());

    renderer.setOrbitMode(true);
    renderer.resetView();

    EXPECT_TRUE(renderer.isOrbitMode());
    renderer.shutdown();
}

// ==================== P0 回归测试 (2026-07-29) ====================

TEST(FrameworkRegressionTest, UiStateSnapshot_AllFieldsHaveDefaults)
{
    // 验证合并后状态快照所有字段（含命令/交互/工具字段）都有稳定默认值
    UiStateSnapshot snapshot;

    EXPECT_TRUE(snapshot.currentSelectionText.isEmpty());
    EXPECT_TRUE(snapshot.activeToolId.isEmpty());
    EXPECT_TRUE(snapshot.inputFocusWidget.isEmpty());
    EXPECT_EQ(snapshot.currentSelectionSource, QStringLiteral("none"));
    EXPECT_EQ(snapshot.currentSelectionType, QStringLiteral("none"));
    EXPECT_EQ(snapshot.currentWorkbenchId, QStringLiteral("default"));
    EXPECT_EQ(snapshot.currentThemeId, QStringLiteral("system"));
    EXPECT_EQ(snapshot.currentViewMode, QStringLiteral("none"));
    EXPECT_EQ(snapshot.currentLayerId, QStringLiteral("default"));
    EXPECT_EQ(snapshot.currentDocumentId, QStringLiteral("none"));
    EXPECT_EQ(snapshot.currentCommandId, QStringLiteral("idle"));
    EXPECT_EQ(snapshot.refreshState, QStringLiteral("idle"));
    EXPECT_EQ(snapshot.progress, -1);
    EXPECT_FALSE(snapshot.busy);
    EXPECT_FALSE(snapshot.dirty);
}

TEST(FrameworkRegressionTest, UiStateSnapshot_RoundTrip)
{
    // 验证快照读写闭环：写入 → 读取 → 值一致
    UiStateSnapshot snapshot;
    snapshot.activeToolId = QStringLiteral("SelectTool");
    snapshot.inputFocusWidget = QStringLiteral("viewport");
    snapshot.currentViewMode = QStringLiteral("3D");
    snapshot.currentLayerId = QStringLiteral("layer0");
    snapshot.currentDocumentId = QStringLiteral("doc_001");
    snapshot.currentSelectionText = QStringLiteral("Line");
    snapshot.dirty = true;

    EXPECT_EQ(snapshot.activeToolId, QStringLiteral("SelectTool"));
    EXPECT_EQ(snapshot.inputFocusWidget, QStringLiteral("viewport"));
    EXPECT_EQ(snapshot.currentViewMode, QStringLiteral("3D"));
    EXPECT_EQ(snapshot.currentLayerId, QStringLiteral("layer0"));
    EXPECT_EQ(snapshot.currentDocumentId, QStringLiteral("doc_001"));
    EXPECT_EQ(snapshot.currentSelectionText, QStringLiteral("Line"));
    EXPECT_TRUE(snapshot.dirty);
}

TEST(FrameworkRegressionTest, SceneGeometryCollector_CollectsBBoxData)
{
    // 验证 SceneGeometryCollector 正确收集包围盒数据
    Eg::SceneGeometryCollector collector;

    // 模拟 ISceneDataSource 推送包围盒数据
    collector.setCurrentEntityId(42);
    Ut::BBox3f bbox(Ut::Vec3f(0, 0, 0), Ut::Vec3f(10, 10, 10));
    collector.emitBBox(bbox);

    collector.setCurrentEntityId(99);
    Ut::BBox3f bbox2(Ut::Vec3f(-5, -5, -5), Ut::Vec3f(5, 5, 5));
    collector.emitBBox(bbox2);

    EXPECT_EQ(collector.bboxCount(), 2u);
    const auto& bboxes = collector.bboxes();
    ASSERT_EQ(bboxes.size(), 2u);

    EXPECT_EQ(bboxes[0].entityId, 42u);
    EXPECT_TRUE(bboxes[0].bbox.isValid());

    EXPECT_EQ(bboxes[1].entityId, 99u);
    EXPECT_TRUE(bboxes[1].bbox.isValid());
}

TEST(FrameworkRegressionTest, SceneGeometryCollector_ClearResetsState)
{
    // 验证 clear() 正确重置收集器状态
    Eg::SceneGeometryCollector collector;

    collector.setCurrentEntityId(1);
    collector.emitBBox(Ut::BBox3f(Ut::Vec3f(0, 0, 0), Ut::Vec3f(1, 1, 1)));
    collector.emitTriangleSoup(nullptr, 0, nullptr, 0, Ut::Color());

    EXPECT_EQ(collector.bboxCount(), 1u);
    EXPECT_EQ(collector.meshCount(), 1u);

    collector.clear();

    EXPECT_EQ(collector.bboxCount(), 0u);
    EXPECT_EQ(collector.meshCount(), 0u);
}

TEST(FrameworkRegressionTest, ISceneDataSource_DefaultImplementations)
{
    // 验证 ISceneDataSource 默认实现的行为（子类可能不覆盖）
    // 创建一个最小实现来测试默认行为
    struct MinimalDataSource : Eg::ISceneDataSource
    {
        void gatherGeometry(Eg::ISceneGeometrySink&) const override {}

        Ut::BBox2d sceneBBox2D() const override
        {
            return Ut::BBox2d();
        }

        Ut::BBox3f sceneBBox3D() const override
        {
            return Ut::BBox3f();
        }

        size_t entityCount() const override
        {
            return 0;
        }
    };

    MinimalDataSource ds;
    EXPECT_EQ(ds.entityCount(), 0u);

    // ABI 收口：forEachSelectedEntityId 默认实现为空遍历
    bool visited = false;
    ds.forEachSelectedEntityId(
        [](Eg::EntityId, void* ctx) {
            *static_cast<bool*>(ctx) = true;
        },
        &visited);
    EXPECT_FALSE(visited);  // 默认实现不遍历任何实体

    // ABI 收口：entityName 改为 buffer 模式，默认实现返回空字符串
    char nameBuf[64] = {};
    EXPECT_EQ(ds.entityName(42, nameBuf, sizeof(nameBuf)), 0u);
    EXPECT_STREQ(nameBuf, "");
}

TEST(FrameworkRegressionTest, ISceneGeometrySink_EmitBBoxDefaultIsNoop)
{
    // 验证 emitBBox 默认实现为空操作（不强制子类覆盖）
    struct MinimalSink : Eg::ISceneGeometrySink
    {
        void emitPolyline(const Ut::Vec2d*, size_t, bool, const Ut::Color&) override {}

        void emitCircle(const Ut::Vec2d&, double, const Ut::Color&) override {}

        void emitArc(const Ut::Vec2d&, double, double, double, const Ut::Color&) override {}

        void emitEllipse(const Ut::Vec2d&, double, double, double, double, double, bool, const Ut::Color&) override {}

        void emitText(const Ut::Vec2d&, const char*, const Ut::Color&) override {}

        void emitImagePlaceholder(
            const Ut::Vec2d&, const Ut::Vec2d&, const Ut::Vec2d&, const Ut::Vec2d&, const Ut::Color&) override
        {
        }

        void emitTriangleSoup(const Ut::Vec3f*, size_t, const Ut::Vec3f*, size_t, const Ut::Color&) override {}
    };

    MinimalSink sink;
    // emitBBox 默认实现不应抛异常
    sink.emitBBox(Ut::BBox3f());
    SUCCEED();
}

// ==================== 场景序列化往返测试 (2026-07-30) ====================

TEST(FrameworkRegressionTest, SerializationRoundtrip_EntityCountAndTypes)
{
    // 创建包含多种图元的文档，序列化到内存后反序列化，验证图元数量和类型匹配
    Fio::SyDocument doc;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setName("TestLine");

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(5, 5);
    circle->dRadius = 3.0;
    circle->setName("TestCircle");

    auto polygon = std::make_unique<Eg::SyPolygon>();
    polygon->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10), Ut::Vec2d(0, 10) });
    polygon->setName("TestPolygon");

    polygon->bClosed = true;

    doc.addEntity(line.release());
    doc.addEntity(circle.release());
    doc.addEntity(polygon.release());

    EXPECT_EQ(doc.entityCount(), 3u);

    // 序列化到内存
    Fio::SySerializer serializer;
    std::vector<uint8_t> buffer;
    auto saveResult = serializeDoc(serializer, doc, buffer);
    ASSERT_TRUE(saveResult.success) << saveResult.errorMessage;
    EXPECT_FALSE(buffer.empty());

    // 反序列化
    Fio::SyDocument loaded;
    auto loadResult = deserializeDoc(serializer, buffer, loaded);
    ASSERT_TRUE(loadResult.success) << loadResult.errorMessage;

    // 验证图元数量和类型
    ASSERT_EQ(loaded.entityCount(), 3u);
    EXPECT_EQ(loaded.entityAt(0)->eType, Eg::EType::LINE);
    EXPECT_EQ(loaded.entityAt(1)->eType, Eg::EType::CIRCLE);
    EXPECT_EQ(loaded.entityAt(2)->eType, Eg::EType::POLYGON);
}

TEST(FrameworkRegressionTest, SerializationRoundtrip_EntityProperties)
{
    // 验证序列化往返后图元属性（位置、顶点、闭合标记）保持不变
    Fio::SyDocument doc;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(1.5, 2.5), Ut::Vec2d(3.5, 4.5) });
    line->setName("PropLine");

    line->bClosed = false;
    doc.addEntity(line.release());

    auto circle = std::make_unique<Eg::SyCircle>();
    circle->basePoint = Ut::Vec2d(7.5, 8.5);
    circle->dRadius = 4.0;
    circle->setName("PropCircle");

    circle->bClosed = true;
    doc.addEntity(circle.release());

    auto polygon = std::make_unique<Eg::SyPolygon>();
    polygon->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(5, 0), Ut::Vec2d(5, 5) });
    polygon->setName("PropPolygon");

    polygon->bClosed = true;
    doc.addEntity(polygon.release());

    Fio::SySerializer serializer;
    std::vector<uint8_t> buffer;
    ASSERT_TRUE(serializeDoc(serializer, doc, buffer).success);

    Fio::SyDocument loaded;
    ASSERT_TRUE(deserializeDoc(serializer, buffer, loaded).success);
    ASSERT_EQ(loaded.entityCount(), 3u);

    // 验证线属性
    auto* loadedLine = dynamic_cast<Eg::SyLine*>(loaded.entityAt(0));
    ASSERT_NE(loadedLine, nullptr);
    ASSERT_EQ(loadedLine->pointRef().size(), 2u);
    EXPECT_DOUBLE_EQ(loadedLine->pointRef()[0].x(), 1.5);
    EXPECT_DOUBLE_EQ(loadedLine->pointRef()[0].y(), 2.5);
    EXPECT_DOUBLE_EQ(loadedLine->pointRef()[1].x(), 3.5);
    EXPECT_DOUBLE_EQ(loadedLine->pointRef()[1].y(), 4.5);
    EXPECT_STREQ(loadedLine->name(), "PropLine");
    EXPECT_FALSE(loadedLine->bClosed);

    // 验证圆属性
    auto* loadedCircle = dynamic_cast<Eg::SyCircle*>(loaded.entityAt(1));
    ASSERT_NE(loadedCircle, nullptr);
    EXPECT_DOUBLE_EQ(loadedCircle->basePoint.x(), 7.5);
    EXPECT_DOUBLE_EQ(loadedCircle->basePoint.y(), 8.5);
    EXPECT_DOUBLE_EQ(loadedCircle->dRadius, 4.0);
    EXPECT_STREQ(loadedCircle->name(), "PropCircle");
    EXPECT_TRUE(loadedCircle->bClosed);

    // 验证多边形属性
    auto* loadedPolygon = dynamic_cast<Eg::SyPolygon*>(loaded.entityAt(2));
    ASSERT_NE(loadedPolygon, nullptr);
    EXPECT_EQ(loadedPolygon->vertices().size(), 3u);
    EXPECT_STREQ(loadedPolygon->name(), "PropPolygon");
    EXPECT_TRUE(loadedPolygon->bClosed);
}

// ==================== 2D/3D 视口切换回归测试 (2026-07-30) ====================

TEST(FrameworkRegressionTest, ViewportSwitch_WorkbenchSnapshotPreservesState)
{
    // 验证合并后状态快照在视口切换时正确保存和恢复状态
    UiStateSnapshot snapshot2D;
    snapshot2D.currentViewMode = QStringLiteral("2D");
    snapshot2D.metadata.insert(QStringLiteral("viewportType"), QStringLiteral("2D_Viewport"));
    snapshot2D.metadata.insert(QStringLiteral("viewportStatus"), QStringLiteral("zoom=1.5;pan=100,200"));
    snapshot2D.currentDocumentId = QStringLiteral("doc_001");
    snapshot2D.currentSelectionSource = QStringLiteral("viewport");
    snapshot2D.currentSelectionType = QStringLiteral("Line");
    snapshot2D.activeToolId = QStringLiteral("DrawLineTool");
    snapshot2D.dirty = false;

    // 保存 2D 快照（模拟 2D -> 3D 切换）
    UiStateSnapshot saved2D = snapshot2D;

    // 切换到 3D 视口
    UiStateSnapshot snapshot3D;
    snapshot3D.currentViewMode = QStringLiteral("3D");
    snapshot3D.metadata.insert(QStringLiteral("viewportType"), QStringLiteral("3D_Viewport"));
    snapshot3D.metadata.insert(QStringLiteral("viewportStatus"), QStringLiteral("zoom=2.0;pan=50,50;orbit=30,45"));
    snapshot3D.activeToolId = QStringLiteral("OrbitTool");
    snapshot3D.currentDocumentId = QStringLiteral("doc_001");  // 活动文档应保持不变

    // 验证 3D 和 2D 状态不同（视口状态经 metadata 承载，与生产一致）
    EXPECT_NE(saved2D.currentViewMode, snapshot3D.currentViewMode);
    EXPECT_NE(saved2D.metadata.value(QStringLiteral("viewportType")),
        snapshot3D.metadata.value(QStringLiteral("viewportType")));
    // 活动文档在切换时保持不变
    EXPECT_EQ(saved2D.currentDocumentId, snapshot3D.currentDocumentId);

    // 保存 3D 快照（模拟 3D -> 2D 切换）
    UiStateSnapshot saved3D = snapshot3D;

    // 恢复 2D 状态
    UiStateSnapshot restored2D = saved2D;

    // 验证恢复到 2D 状态
    EXPECT_EQ(restored2D.currentViewMode, QStringLiteral("2D"));
    EXPECT_EQ(restored2D.metadata.value(QStringLiteral("viewportType")), QStringLiteral("2D_Viewport"));
    EXPECT_EQ(restored2D.metadata.value(QStringLiteral("viewportStatus")), QStringLiteral("zoom=1.5;pan=100,200"));
    EXPECT_EQ(restored2D.currentDocumentId, QStringLiteral("doc_001"));
    EXPECT_EQ(restored2D.activeToolId, QStringLiteral("DrawLineTool"));
    EXPECT_FALSE(restored2D.dirty);

    // 3D 快照也应保持独立
    EXPECT_EQ(saved3D.currentViewMode, QStringLiteral("3D"));
    EXPECT_EQ(saved3D.activeToolId, QStringLiteral("OrbitTool"));
}

TEST(FrameworkRegressionTest, ViewportSwitch_SelectionStateRecoverable)
{
    // 验证选择状态在视口切换后可以恢复
    UiStateSnapshot snapshot;
    snapshot.currentSelectionSource = QStringLiteral("viewport");
    snapshot.currentSelectionText = QStringLiteral("Line (id=42)");
    snapshot.currentSelectionType = QStringLiteral("Line");
    snapshot.currentDocumentId = QStringLiteral("doc_001");

    // 保存 2D 选择状态
    UiStateSnapshot saved2D = snapshot;

    // 切换到 3D，清除选择状态
    snapshot.currentSelectionSource.clear();
    snapshot.currentSelectionText.clear();
    snapshot.currentSelectionType.clear();
    snapshot.currentViewMode = QStringLiteral("3D");

    EXPECT_TRUE(snapshot.currentSelectionSource.isEmpty());
    EXPECT_TRUE(snapshot.currentSelectionText.isEmpty());
    EXPECT_TRUE(snapshot.currentSelectionType.isEmpty());

    // 恢复 2D，选择状态应恢复
    UiStateSnapshot restored = saved2D;
    EXPECT_EQ(restored.currentSelectionSource, QStringLiteral("viewport"));
    EXPECT_EQ(restored.currentSelectionText, QStringLiteral("Line (id=42)"));
    EXPECT_EQ(restored.currentSelectionType, QStringLiteral("Line"));
}

TEST(FrameworkRegressionTest, ViewportSwitch_ViewportStatePerViewport)
{
    // 验证每个视口的视口状态（缩放、平移）独立保存（经 metadata 承载）
    UiStateSnapshot snapshot2D;
    snapshot2D.currentViewMode = QStringLiteral("2D");
    snapshot2D.metadata.insert(QStringLiteral("viewportType"), QStringLiteral("2D_Viewport"));
    snapshot2D.metadata.insert(QStringLiteral("viewportStatus"), QStringLiteral("zoom=1.0;pan=0,0"));

    UiStateSnapshot snapshot3D;
    snapshot3D.currentViewMode = QStringLiteral("3D");
    snapshot3D.metadata.insert(QStringLiteral("viewportType"), QStringLiteral("3D_Viewport"));
    snapshot3D.metadata.insert(QStringLiteral("viewportStatus"), QStringLiteral("zoom=2.5;pan=100,50;orbit=45,30"));

    // 验证两个视口快照的视口状态独立
    EXPECT_NE(snapshot2D.metadata.value(QStringLiteral("viewportStatus")),
        snapshot3D.metadata.value(QStringLiteral("viewportStatus")));
    EXPECT_NE(snapshot2D.metadata.value(QStringLiteral("viewportType")),
        snapshot3D.metadata.value(QStringLiteral("viewportType")));

    // 修改 2D 视口状态不应影响 3D 快照
    UiStateSnapshot saved3D = snapshot3D;
    snapshot2D.metadata.insert(QStringLiteral("viewportStatus"), QStringLiteral("zoom=1.5;pan=200,100"));

    // 3D 快照保持不变
    EXPECT_EQ(saved3D.metadata.value(QStringLiteral("viewportStatus")), QStringLiteral("zoom=2.5;pan=100,50;orbit=45,30"));
    EXPECT_EQ(snapshot3D.metadata.value(QStringLiteral("viewportStatus")), saved3D.metadata.value(QStringLiteral("viewportStatus")));
}

// ==================== 渲染快照一致性测试 (2026-07-30) ====================

TEST(FrameworkRegressionTest, SceneGeometryCollector_DeterministicOutput)
{
    // 验证 SceneGeometryCollector 对相同输入产生确定性输出
    Eg::SceneGeometryCollector collector1;
    Eg::SceneGeometryCollector collector2;

    // 第一次收集
    collector1.setCurrentEntityId(1);
    collector1.emitBBox(Ut::BBox3f(Ut::Vec3f(0, 0, 0), Ut::Vec3f(10, 10, 10)));
    collector1.setCurrentEntityId(2);
    const Ut::Vec3f verts[] = { Ut::Vec3f(0, 0, 0), Ut::Vec3f(1, 0, 0), Ut::Vec3f(0, 1, 0) };
    const Ut::Vec3f norms[] = { Ut::Vec3f(0, 0, 1), Ut::Vec3f(0, 0, 1), Ut::Vec3f(0, 0, 1) };
    collector1.emitTriangleSoup(verts, 3, norms, 3, Ut::Color());

    // 第二次收集（相同输入）
    collector2.setCurrentEntityId(1);
    collector2.emitBBox(Ut::BBox3f(Ut::Vec3f(0, 0, 0), Ut::Vec3f(10, 10, 10)));
    collector2.setCurrentEntityId(2);
    collector2.emitTriangleSoup(verts, 3, norms, 3, Ut::Color());

    // 验证结果一致
    EXPECT_EQ(collector1.bboxCount(), collector2.bboxCount());
    EXPECT_EQ(collector1.meshCount(), collector2.meshCount());

    const auto& bboxes1 = collector1.bboxes();
    const auto& bboxes2 = collector2.bboxes();
    ASSERT_EQ(bboxes1.size(), bboxes2.size());

    for (size_t i = 0; i < bboxes1.size(); ++i)
    {
        EXPECT_EQ(bboxes1[i].entityId, bboxes2[i].entityId);
        EXPECT_EQ(bboxes1[i].bbox.minPt[0], bboxes2[i].bbox.minPt[0]);
        EXPECT_EQ(bboxes1[i].bbox.minPt[1], bboxes2[i].bbox.minPt[1]);
        EXPECT_EQ(bboxes1[i].bbox.minPt[2], bboxes2[i].bbox.minPt[2]);
    }

    const auto& meshes1 = collector1.meshes();
    const auto& meshes2 = collector2.meshes();
    ASSERT_EQ(meshes1.size(), meshes2.size());

    for (size_t i = 0; i < meshes1.size(); ++i)
    {
        EXPECT_EQ(meshes1[i].entityId, meshes2[i].entityId);
        EXPECT_EQ(meshes1[i].vertices.size(), meshes2[i].vertices.size());
        EXPECT_EQ(meshes1[i].normals.size(), meshes2[i].normals.size());
    }
}

TEST(FrameworkRegressionTest, ISceneDataSource_GatherGeometryDeterministic)
{
    // 验证 ISceneDataSource::gatherGeometry() 对相同场景产生确定性输出
    struct DeterministicSource : Eg::ISceneDataSource
    {
        void gatherGeometry(Eg::ISceneGeometrySink& sink) const override
        {
            sink.setCurrentEntityId(10);
            sink.emitBBox(Ut::BBox3f(Ut::Vec3f(-1, -1, -1), Ut::Vec3f(1, 1, 1)));

            sink.setCurrentEntityId(20);
            // ABI 收口：emitTriangleSoup 参数改为原始指针
            Ut::Vec3f verts[] = { Ut::Vec3f(0, 0, 0), Ut::Vec3f(2, 0, 0), Ut::Vec3f(0, 2, 0) };
            Ut::Vec3f norms[] = { Ut::Vec3f(0, 0, 1), Ut::Vec3f(0, 0, 1), Ut::Vec3f(0, 0, 1) };
            sink.emitTriangleSoup(verts, 3, norms, 3, Ut::Color());
        }

        Ut::BBox2d sceneBBox2D() const override
        {
            return Ut::BBox2d();
        }

        Ut::BBox3f sceneBBox3D() const override
        {
            return Ut::BBox3f();
        }

        size_t entityCount() const override
        {
            return 2;
        }
    };

    DeterministicSource source;

    Eg::SceneGeometryCollector collector1;
    Eg::SceneGeometryCollector collector2;

    source.gatherGeometry(collector1);
    source.gatherGeometry(collector2);

    // 两次调用产生完全相同的结果
    EXPECT_EQ(collector1.bboxCount(), collector2.bboxCount());
    EXPECT_EQ(collector1.meshCount(), collector2.meshCount());
    EXPECT_EQ(collector1.bboxCount(), 1u);
    EXPECT_EQ(collector1.meshCount(), 1u);

    // 验证收集到的数据内容一致
    const auto& bboxes1 = collector1.bboxes();
    const auto& bboxes2 = collector2.bboxes();
    ASSERT_EQ(bboxes1.size(), bboxes2.size());
    EXPECT_EQ(bboxes1[0].entityId, bboxes2[0].entityId);

    const auto& meshes1 = collector1.meshes();
    const auto& meshes2 = collector2.meshes();
    ASSERT_EQ(meshes1.size(), meshes2.size());
    EXPECT_EQ(meshes1[0].entityId, meshes2[0].entityId);
    EXPECT_EQ(meshes1[0].vertices.size(), meshes2[0].vertices.size());
}

TEST(FrameworkRegressionTest, SceneGeometryCollector_ConsistentAcrossMultipleCalls)
{
    // 验证 SceneGeometryCollector 在多次调用后仍保持一致性
    Eg::SceneGeometryCollector collector;

    // 第一次收集
    collector.setCurrentEntityId(1);
    collector.emitBBox(Ut::BBox3f(Ut::Vec3f(0, 0, 0), Ut::Vec3f(5, 5, 5)));

    size_t firstBBoxCount = collector.bboxCount();
    size_t firstMeshCount = collector.meshCount();

    // 第二次收集（不清理，累加）
    collector.setCurrentEntityId(2);
    collector.emitBBox(Ut::BBox3f(Ut::Vec3f(10, 10, 10), Ut::Vec3f(15, 15, 15)));

    size_t secondBBoxCount = collector.bboxCount();
    size_t secondMeshCount = collector.meshCount();

    EXPECT_GT(secondBBoxCount, firstBBoxCount);
    EXPECT_EQ(secondMeshCount, firstMeshCount);

    // 清理后重新收集，验证状态重置
    collector.clear();
    EXPECT_EQ(collector.bboxCount(), 0u);
    EXPECT_EQ(collector.meshCount(), 0u);

    collector.setCurrentEntityId(1);
    collector.emitBBox(Ut::BBox3f(Ut::Vec3f(0, 0, 0), Ut::Vec3f(5, 5, 5)));

    EXPECT_EQ(collector.bboxCount(), 1u);
    EXPECT_EQ(collector.meshCount(), 0u);
}

// ==================== 撤销/重做回归测试 (2026-07-30) ====================

TEST(FrameworkRegressionTest, UndoRedo_CreateUndoEntityRemoved)
{
    // 创建图元 → 撤销 → 验证图元被移除
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId entityId = line->id;

    // 创建（添加图元）
    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 撤销（提取图元，模拟 undo）
    auto extracted = scene.extractEntityById(entityId);
    EXPECT_NE(extracted, nullptr);
    EXPECT_EQ(scene.getEntityCount(), 0u);
    EXPECT_EQ(extracted->id, entityId);

    // 验证提取的图元属性完整
    auto* extractedLine = dynamic_cast<Eg::SyLine*>(extracted.get());
    ASSERT_NE(extractedLine, nullptr);
    EXPECT_EQ(extractedLine->pointRef().size(), 2u);
}

TEST(FrameworkRegressionTest, UndoRedo_RedoEntityRestoredWithProperties)
{
    // 撤销后再重做，验证图元恢复且属性正确
    Eg::SceneManager scene;

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(1, 2), Ut::Vec2d(3, 4) });
    line->setName("RedoLine");

    Eg::EntityId entityId = line->id;
    Ut::Vec2d pt0 = line->pointRef()[0];
    Ut::Vec2d pt1 = line->pointRef()[1];

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 撤销
    auto extracted = scene.extractEntityById(entityId);
    ASSERT_NE(extracted, nullptr);
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // 重做
    bool inserted = scene.insertEntityPreserveId(std::move(extracted));
    EXPECT_TRUE(inserted);
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 验证属性恢复
    auto* restored = scene.findSyEntityById(entityId);
    ASSERT_NE(restored, nullptr);
    EXPECT_STREQ(restored->name(), "RedoLine");

    auto* restoredLine = dynamic_cast<Eg::SyLine*>(restored);
    ASSERT_NE(restoredLine, nullptr);
    EXPECT_EQ(restoredLine->pointRef().size(), 2u);
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[0].x(), pt0.x());
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[0].y(), pt0.y());
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[1].x(), pt1.x());
    EXPECT_DOUBLE_EQ(restoredLine->pointRef()[1].y(), pt1.y());
}

TEST(FrameworkRegressionTest, UndoRedo_MultipleUndoRedoSequence)
{
    // 多次撤销/重做序列：创建 → 修改 → 删除 → undo ×3 → redo ×3
    Eg::SceneManager scene;

    // 步骤1: 创建实体
    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    line->setName("Original");

    Eg::EntityId entityId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));
    EXPECT_EQ(scene.getEntityCount(), 1u);

    // 步骤2: 修改（通过 extract + insert 模拟替换）
    auto oldState = scene.extractEntityById(entityId);
    ASSERT_NE(oldState, nullptr);
    EXPECT_STREQ(oldState->name(), "Original");

    auto modified = std::make_unique<Eg::SyLine>();
    modified->id = entityId;
    modified->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(20, 20) });
    modified->setName("Modified");

    scene.insertEntityPreserveId(std::move(modified));

    auto* current = scene.findSyEntityById(entityId);
    ASSERT_NE(current, nullptr);
    EXPECT_STREQ(current->name(), "Modified");

    // 步骤3: 删除
    auto deletedState = scene.extractEntityById(entityId);
    ASSERT_NE(deletedState, nullptr);
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // Undo 1: 恢复删除
    scene.insertEntityPreserveId(std::move(deletedState));
    EXPECT_EQ(scene.getEntityCount(), 1u);
    current = scene.findSyEntityById(entityId);
    ASSERT_NE(current, nullptr);
    EXPECT_STREQ(current->name(), "Modified");

    // Undo 2: 恢复修改（还原为原始状态）
    auto modifiedState = scene.extractEntityById(entityId);
    ASSERT_NE(modifiedState, nullptr);
    scene.insertEntityPreserveId(std::move(oldState));
    EXPECT_EQ(scene.getEntityCount(), 1u);
    current = scene.findSyEntityById(entityId);
    ASSERT_NE(current, nullptr);
    EXPECT_STREQ(current->name(), "Original");

    // Undo 3: 恢复创建（移除图元）
    auto originalState = scene.extractEntityById(entityId);
    ASSERT_NE(originalState, nullptr);
    EXPECT_EQ(scene.getEntityCount(), 0u);

    // Redo 1: 重建创建
    scene.insertEntityPreserveId(std::move(originalState));
    EXPECT_EQ(scene.getEntityCount(), 1u);
    current = scene.findSyEntityById(entityId);
    ASSERT_NE(current, nullptr);
    EXPECT_STREQ(current->name(), "Original");

    // Redo 2: 重建修改
    auto origForRedo = scene.extractEntityById(entityId);
    ASSERT_NE(origForRedo, nullptr);
    scene.insertEntityPreserveId(std::move(modifiedState));
    EXPECT_EQ(scene.getEntityCount(), 1u);
    current = scene.findSyEntityById(entityId);
    ASSERT_NE(current, nullptr);
    EXPECT_STREQ(current->name(), "Modified");

    // Redo 3: 重建删除
    auto finalDeleted = scene.extractEntityById(entityId);
    ASSERT_NE(finalDeleted, nullptr);
    EXPECT_EQ(scene.getEntityCount(), 0u);
}