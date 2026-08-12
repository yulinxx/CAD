/**
 * @file Scene3DRegressionTests.cpp
 * @brief 3D 侧最小闭环回归测试 — 覆盖 SceneManager3D / SimpleRenderer3D / RenderWidget3DAdapter
 *
 * 测试范围：
 *  - SceneManager3D 生命周期与实体管理
 *  - 3D 场景实体添加/删除/选中
 *  - SimpleRenderer3D 与场景联动
 *  - RenderWidget3DAdapter 回调链路
 *  - 3D 相机模式切换
 *
 * 新增测试 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "UI/Render/SimpleRenderer3D.h"
#include "UI/Render/RenderWidget3DAdapter.h"
#include "UI/IRenderSurface.h"
#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"

#include <memory>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>

// ==================== SceneManager3D 基础测试 ====================

namespace
{
// 构造一个带单个三角形（合法网格）的图元，满足 SceneManager3D::addEntity 的 isValid() 约束
std::unique_ptr<Eg::SyMeshEntity> makeTriangleMesh(const char *name = nullptr)
{
    auto mesh = std::make_unique<Eg::SyMeshEntity>(name ? name : "");
    mesh->vertices = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
    mesh->normals = mesh->vertices;
    return mesh;
}
} // namespace

TEST(Scene3DRegressionTest, SceneManager3D_DefaultConstruction)
{
    Eg::SceneManager3D scene;
    SUCCEED();
}

TEST(Scene3DRegressionTest, SceneManager3D_AddMeshEntity)
{
    Eg::SceneManager3D scene;

    auto mesh = makeTriangleMesh("TestMesh");

    Eg::EntityId meshId = mesh->id;
    scene.addEntity(mesh.release());

    EXPECT_EQ(scene.getEntityCount(), 1u);
    auto *found = scene.findEntityById(meshId);
    ASSERT_NE(found, nullptr);
    EXPECT_STREQ(found->name(), "TestMesh");
}

TEST(Scene3DRegressionTest, SceneManager3D_ClearScene)
{
    Eg::SceneManager3D scene;

    auto mesh1 = makeTriangleMesh();
    auto mesh2 = makeTriangleMesh();
    scene.addEntity(mesh1.release());
    scene.addEntity(mesh2.release());

    EXPECT_EQ(scene.getEntityCount(), 2u);

    scene.clearScene();
    EXPECT_EQ(scene.getEntityCount(), 0u);
}

TEST(Scene3DRegressionTest, SceneManager3D_AddMultipleEntities)
{
    Eg::SceneManager3D scene;

    for (int i = 0; i < 10; ++i)
    {
        auto mesh = makeTriangleMesh(("Mesh_" + std::to_string(i)).c_str());

        scene.addEntity(mesh.release());
    }

    EXPECT_EQ(scene.getEntityCount(), 10u);
}

TEST(Scene3DRegressionTest, SceneManager3D_ClearAndReAdd)
{
    Eg::SceneManager3D scene;

    auto mesh = makeTriangleMesh();
    scene.addEntity(mesh.release());
    EXPECT_EQ(scene.getEntityCount(), 1u);

    scene.clearScene();
    EXPECT_EQ(scene.getEntityCount(), 0u);

    auto mesh2 = makeTriangleMesh();
    scene.addEntity(mesh2.release());
    EXPECT_EQ(scene.getEntityCount(), 1u);
}

TEST(Scene3DRegressionTest, SceneManager3D_SelectEntity)
{
    Eg::SceneManager3D scene;

    auto mesh = makeTriangleMesh("SelectableMesh");

    auto *rawPtr = mesh.get();
    scene.addEntity(mesh.release());

    scene.selectEntity(rawPtr);
    size_t selCount = 0;
    scene.forEachSelectedEntityId(
        [](Eg::EntityId, void *ctx) -> bool {
            ++*static_cast<size_t *>(ctx);
            return true;
        },
        &selCount);
    EXPECT_EQ(selCount, 1u);
    EXPECT_NE(scene.getSelectedEntity(), nullptr);
}

TEST(Scene3DRegressionTest, SceneManager3D_ClearSelection)
{
    Eg::SceneManager3D scene;

    auto mesh = makeTriangleMesh();
    auto *rawPtr = mesh.get();
    scene.addEntity(mesh.release());

    scene.selectEntity(rawPtr);
    size_t selCount1 = 0;
    scene.forEachSelectedEntityId(
        [](Eg::EntityId, void *ctx) -> bool {
            ++*static_cast<size_t *>(ctx);
            return true;
        },
        &selCount1);
    EXPECT_EQ(selCount1, 1u);

    scene.clearSelection();
    size_t selCount2 = 0;
    scene.forEachSelectedEntityId(
        [](Eg::EntityId, void *ctx) -> bool {
            ++*static_cast<size_t *>(ctx);
            return true;
        },
        &selCount2);
    EXPECT_EQ(selCount2, 0u);
}

TEST(Scene3DRegressionTest, SceneManager3D_DeleteSelected)
{
    Eg::SceneManager3D scene;

    auto mesh = makeTriangleMesh();
    auto *rawPtr = mesh.get();
    scene.addEntity(mesh.release());

    scene.selectEntity(rawPtr);
    EXPECT_EQ(scene.getEntityCount(), 1u);

    scene.deleteSelected();
    EXPECT_EQ(scene.getEntityCount(), 0u);
    size_t selCount = 0;
    scene.forEachSelectedEntityId(
        [](Eg::EntityId, void *ctx) -> bool {
            ++*static_cast<size_t *>(ctx);
            return true;
        },
        &selCount);
    EXPECT_EQ(selCount, 0u);
}

// ==================== SimpleRenderer3D 集成冒烟测试 ====================
// 仅保留与场景/接口对接有关的最小覆盖，具体渲染器行为归 `SimpleRenderer3DTests.cpp` 负责。

TEST(Scene3DRegressionTest, SimpleRenderer3D_RenderWithoutScene)
{
    // MainTests 为 gtest 控制台程序，无 QApplication。QPainter::drawText 在无 QGuiApplication
    // 时字体引擎未初始化会崩溃（0xC0000409）；与 SimpleRenderer3DTests.cpp 的 Render 测试约定一致：
    // 无 GUI 上下文时跳过实际渲染，仅保留可安全执行的冒烟部分。
    if (!qGuiApp)
        GTEST_SKIP() << "Render test requires Qt GUI context";

    SimpleRenderer3D renderer;
    renderer.initialize();

    QImage image(400, 300, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    renderer.render(painter, 400, 300);
    // 无场景时渲染不应崩溃
    SUCCEED();

    renderer.shutdown();
}

TEST(Scene3DRegressionTest, SimpleRenderer3D_ResizePreservesState)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.resize(800, 600);
    EXPECT_TRUE(renderer.isReady());

    renderer.resize(1024, 768);
    EXPECT_TRUE(renderer.isReady());

    renderer.shutdown();
}

TEST(Scene3DRegressionTest, SimpleRenderer3D_ResetView)
{
    SimpleRenderer3D renderer;
    renderer.initialize();
    renderer.resetView();
    EXPECT_TRUE(renderer.isReady());
    renderer.shutdown();
}

TEST(Scene3DRegressionTest, SimpleRenderer3D_SelectNodeById)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    renderer.selectNodeById("test_node");
    // 选择不存在的节点不应崩溃
    SUCCEED();

    renderer.shutdown();
}

TEST(Scene3DRegressionTest, SimpleRenderer3D_SelectedNodeId_InitiallyEmpty)
{
    SimpleRenderer3D renderer;
    renderer.initialize();

    EXPECT_TRUE(renderer.selectedNodeId().isEmpty());
    EXPECT_TRUE(renderer.selectedPathNames().isEmpty());

    renderer.shutdown();
}

// ==================== RenderWidget3DAdapter 回调测试 ====================

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_DefaultConstruction)
{
    RenderWidget3DAdapter adapter;
    EXPECT_FALSE(adapter.isReady());
    EXPECT_FALSE(adapter.isRenderLoopRunning());
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_InitializeShutdown)
{
    RenderWidget3DAdapter adapter;
    // MainTests 为无 QApplication 的控制台环境，无法提供有效 QWidget 父窗口；
    // initialize() 应优雅返回 false（不做非法内存访问），不应视为失败
    bool result = adapter.initialize();
    EXPECT_FALSE(result);
    EXPECT_FALSE(adapter.isReady());

    adapter.shutdown();
    EXPECT_FALSE(adapter.isReady());
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_StatusCallback)
{
    RenderWidget3DAdapter adapter;
    adapter.initialize();

    QString lastStatus;
    adapter.setStatusCallback([&](const QString &msg) { lastStatus = msg; });

    SUCCEED();
    adapter.shutdown();
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_SelectionCallback)
{
    RenderWidget3DAdapter adapter;
    adapter.initialize();

    QString lastId;
    adapter.setSelectionCallback([&](const QString &id) { lastId = id; });

    SUCCEED();
    adapter.shutdown();
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_PathCallback)
{
    RenderWidget3DAdapter adapter;
    adapter.initialize();

    QStringList lastPaths;
    adapter.setPathCallback([&](const QStringList &paths) { lastPaths = paths; });

    SUCCEED();
    adapter.shutdown();
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_IsOpenGL)
{
    RenderWidget3DAdapter adapter;
    EXPECT_TRUE(adapter.isOpenGL()); // Adapter 使用 OpenGL 渲染
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_OrbitMode)
{
    RenderWidget3DAdapter adapter;
    adapter.initialize();

    // 适配器为过渡层：导航模式由内部 RenderWidget3D 管理，isOrbitMode() 固定返回 true（接口一致性）
    EXPECT_TRUE(adapter.isOrbitMode());

    adapter.setOrbitMode(true);
    EXPECT_TRUE(adapter.isOrbitMode());

    adapter.setOrbitMode(false);
    EXPECT_TRUE(adapter.isOrbitMode());

    adapter.shutdown();
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_MeasureMode)
{
    RenderWidget3DAdapter adapter;
    adapter.initialize();

    adapter.setMeasureMode(true);
    adapter.setMeasureMode(false);

    adapter.shutdown();
    SUCCEED();
}

TEST(Scene3DRegressionTest, RenderWidget3DAdapter_SelectNodeById)
{
    RenderWidget3DAdapter adapter;
    adapter.initialize();

    adapter.selectNodeById("nonexistent");
    EXPECT_TRUE(adapter.selectedNodeId().isEmpty());

    adapter.shutdown();
}

// ==================== IRenderSurface 接口一致性测试 ====================

TEST(Scene3DRegressionTest, SimpleRenderer3D_ImplementsIRenderSurface)
{
    SimpleRenderer3D renderer;
    // 验证 SimpleRenderer3D 实现了 IRenderSurface 接口
    UI::IRenderSurface *surface = dynamic_cast<UI::IRenderSurface *>(&renderer);
    EXPECT_NE(surface, nullptr);
}

TEST(Scene3DRegressionTest, SimpleRenderer3D_IsNotOpenGL)
{
    SimpleRenderer3D renderer;
    EXPECT_FALSE(renderer.isOpenGL()); // SimpleRenderer3D 是软件渲染
}