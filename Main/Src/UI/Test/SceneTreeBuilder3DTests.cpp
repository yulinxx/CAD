/**
 * @file SceneTreeBuilder3DTests.cpp
 * @brief SceneTreeBuilder3D（3D 场景树算法层）单元测试
 *
 * 覆盖：
 *  - 空场景返回空模型
 *  - 构建节点（id/名称/类型/附加信息/选中态）
 *  - 选中计数与 selectedIds
 */

#include <gtest/gtest.h>

#include "SceneTreeBuilder3D.h"
#include "SceneTreeModel3D.h"

#include "Engine3D/SceneManager3D.h"
#include "Engine3D/SyEntity/SyMeshEntity.h"

#include <memory>
#include <QSet>

namespace
{
    // 构造一个带单个三角形（合法网格）的图元，满足 SceneManager3D::addEntity 的 isValid() 约束
    std::unique_ptr<Eg::SyMeshEntity> makeTriangleMesh(const char* name = nullptr)
    {
        auto mesh = std::make_unique<Eg::SyMeshEntity>(name ? name : "");
        mesh->vertices = { { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f } };
        mesh->normals = mesh->vertices;
        return mesh;
    }
}  // namespace

TEST(SceneTreeBuilder3DTest, Build_WithNullScene_ReturnsEmptyModel)
{
    const SceneTreeModel3D model = SceneTreeBuilder3D::build(nullptr);
    EXPECT_EQ(0, model.nodes.size());
    EXPECT_EQ(0, model.totalCount);
    EXPECT_EQ(0, model.selectedCount);
}

TEST(SceneTreeBuilder3DTest, Build_WithEmptyScene_ReturnsEmptyModel)
{
    Eg::SceneManager3D scene;
    const SceneTreeModel3D model = SceneTreeBuilder3D::build(&scene);
    EXPECT_EQ(0, model.nodes.size());
    EXPECT_EQ(0, model.totalCount);
}

TEST(SceneTreeBuilder3DTest, Build_PopulatesMeshNodes)
{
    Eg::SceneManager3D scene;
    auto first = makeTriangleMesh("Body");
    const Eg::EntityId firstId = first->id;
    scene.addEntity(first.release());

    auto second = makeTriangleMesh();
    scene.addEntity(second.release());

    const SceneTreeModel3D model = SceneTreeBuilder3D::build(&scene);
    ASSERT_EQ(2, model.nodes.size());
    EXPECT_EQ(2, model.totalCount);
    EXPECT_EQ(0, model.selectedCount);

    const SceneTreeNode3D& node = model.nodes.at(0);
    EXPECT_EQ(QString::number(firstId), node.id);
    EXPECT_EQ(QStringLiteral("Body"), node.displayName);
    EXPECT_EQ(QStringLiteral("Mesh"), node.typeName);
    EXPECT_EQ(QStringLiteral("1 tris"), node.info);
    EXPECT_TRUE(node.visible);
    EXPECT_FALSE(node.selected);
    EXPECT_FALSE(node.locked);
}

TEST(SceneTreeBuilder3DTest, Build_FallsBackToTypeNameWhenNameless)
{
    Eg::SceneManager3D scene;
    scene.addEntity(makeTriangleMesh().release());

    const SceneTreeModel3D model = SceneTreeBuilder3D::build(&scene);
    ASSERT_EQ(1, model.nodes.size());
    EXPECT_EQ(QStringLiteral("Mesh"), model.nodes.at(0).displayName);
}

TEST(SceneTreeBuilder3DTest, Build_CountsSelectedMeshes)
{
    Eg::SceneManager3D scene;
    auto a = makeTriangleMesh("A");
    auto b = makeTriangleMesh("B");
    auto* bRaw = b.get();
    scene.addEntity(a.release());
    scene.addEntity(b.release());

    // 通过场景选择管理选中第二个图元
    scene.selectEntity(bRaw);

    const SceneTreeModel3D model = SceneTreeBuilder3D::build(&scene);
    EXPECT_EQ(1, model.selectedCount);

    for (const SceneTreeNode3D& node : model.nodes)
    {
        const bool expectSelected = (node.id == QString::number(bRaw->id));
        EXPECT_EQ(expectSelected, node.selected);
    }
}

TEST(SceneTreeBuilder3DTest, SelectedIds_ReturnsSelectedMeshIds)
{
    Eg::SceneManager3D scene;
    auto a = makeTriangleMesh("A");
    auto b = makeTriangleMesh("B");
    auto* bRaw = b.get();
    scene.addEntity(a.release());
    scene.addEntity(b.release());

    scene.selectEntity(bRaw);

    const QSet<QString> ids = SceneTreeBuilder3D::selectedIds(&scene);
    EXPECT_EQ(1, ids.size());
    EXPECT_TRUE(ids.contains(QString::number(bRaw->id)));
}

TEST(SceneTreeBuilder3DTest, SelectedIds_WithNullScene_ReturnsEmpty)
{
    const QSet<QString> ids = SceneTreeBuilder3D::selectedIds(nullptr);
    EXPECT_TRUE(ids.isEmpty());
}
