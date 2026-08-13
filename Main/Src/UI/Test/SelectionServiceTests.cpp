/**
 * @file SelectionServiceTests.cpp
 * @brief 选择服务单元测试 — 覆盖 ISelectionService 窄接口 + Qt 便利方法
 *
 * 测试范围：
 *  - ISelectionService POD 安全接口 (visitSelectedIds/select/selectMultiple/deselect/clear/toggle)
 *  - Qt 便利方法 (selectedIdsQ/selectEntity/setSelectedEntityId/setSelectedEntityIds/entityIdAt)
 *  - 场景管理器集成 (addEntities → selectEntity → 状态一致性)
 *  - 批量操作与切换行为
 *  - 边界条件 (空选择/空场景/重复操作)
 *
 * P5 测试覆盖扩展 (2026-07-30)
 */

#include <gtest/gtest.h>

#include "UI/Services/SelectionService.h"
#include "UI/Services/ISelectionService.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"
#include "Engine2D/SyEntity/SyCircle.h"
#include "Engine2D/SyEntity/SyPolygon.h"

#include <memory>
#include <vector>
#include <string>
#include <cstring>

// ==================== ISelectionService 窄接口测试 ====================

TEST(SelectionServiceTest, ConstructWithSceneManager)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);
    SUCCEED();
}

TEST(SelectionServiceTest, IsSelected_Empty)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    EXPECT_FALSE(svc.isSelected("nonexistent"));
}

TEST(SelectionServiceTest, SelectSingle)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());
    EXPECT_TRUE(svc.isSelected(idStr.c_str()));
}

TEST(SelectionServiceTest, Deselect)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());
    EXPECT_TRUE(svc.isSelected(idStr.c_str()));

    svc.deselect(idStr.c_str());
    EXPECT_FALSE(svc.isSelected(idStr.c_str()));
}

TEST(SelectionServiceTest, Clear)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());
    svc.clear();
    EXPECT_FALSE(svc.isSelected(idStr.c_str()));
}

TEST(SelectionServiceTest, Toggle)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);

    // 切换为选中
    svc.toggle(idStr.c_str());
    EXPECT_TRUE(svc.isSelected(idStr.c_str()));

    // 切换为取消选中
    svc.toggle(idStr.c_str());
    EXPECT_FALSE(svc.isSelected(idStr.c_str()));
}

TEST(SelectionServiceTest, SelectMultiple)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    std::string ids[3];

    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        ids[i] = std::to_string(line->id);
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    const char* idPtrs[] = { ids[0].c_str(), ids[1].c_str(), ids[2].c_str() };
    svc.selectMultiple(idPtrs, 3);

    EXPECT_TRUE(svc.isSelected(ids[0].c_str()));
    EXPECT_TRUE(svc.isSelected(ids[1].c_str()));
    EXPECT_TRUE(svc.isSelected(ids[2].c_str()));
}

TEST(SelectionServiceTest, SelectMultipleClearsPrevious)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    // 先选中一个
    auto allIds = std::vector<Eg::EntityId>{};
    scene.forEachEntityId(
        [](Eg::EntityId eid, void* ctx) -> bool {
            static_cast<std::vector<Eg::EntityId>*>(ctx)->push_back(eid);
            return true;
        },
        &allIds);
    ASSERT_GE(allIds.size(), 3u);
    std::string firstId = std::to_string(allIds[0]);
    svc.select(firstId.c_str());
    EXPECT_TRUE(svc.isSelected(firstId.c_str()));

    // 批量选中另外两个 → 应清除第一个
    std::string id1 = std::to_string(allIds[1]);
    std::string id2 = std::to_string(allIds[2]);
    const char* idPtrs[] = { id1.c_str(), id2.c_str() };
    svc.selectMultiple(idPtrs, 2);

    EXPECT_FALSE(svc.isSelected(firstId.c_str()));
    EXPECT_TRUE(svc.isSelected(id1.c_str()));
    EXPECT_TRUE(svc.isSelected(id2.c_str()));
}

TEST(SelectionServiceTest, VisitSelectedIds)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    auto allIds = std::vector<Eg::EntityId>{};
    scene.forEachEntityId(
        [](Eg::EntityId eid, void* ctx) -> bool {
            static_cast<std::vector<Eg::EntityId>*>(ctx)->push_back(eid);
            return true;
        },
        &allIds);
    ASSERT_GE(allIds.size(), 3u);
    std::string id0 = std::to_string(allIds[0]);
    std::string id1 = std::to_string(allIds[1]);
    const char* idPtrs[] = { id0.c_str(), id1.c_str() };
    svc.selectMultiple(idPtrs, 2);

    int count = 0;
    svc.visitSelectedIds(
        [](const char* id, void* ctx) {
            int* c = static_cast<int*>(ctx);
            (*c)++;
        },
        &count);

    EXPECT_EQ(count, 2);
}

TEST(SelectionServiceTest, VisitSelectedIds_Empty)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    int count = 0;
    svc.visitSelectedIds(
        [](const char* id, void* ctx) {
            int* c = static_cast<int*>(ctx);
            (*c)++;
        },
        &count);

    EXPECT_EQ(count, 0);
}

TEST(SelectionServiceTest, VisitSelectedIds_AfterSelect)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());

    // 通过 ID 遍历验证选择已生效（ISelectionService 不再泄漏 SyEntity*）
    int count = 0;
    svc.visitSelectedIds(
        [](const char* id, void* ctx) {
            int* c = static_cast<int*>(ctx);
            (*c)++;
        },
        &count);

    EXPECT_EQ(count, 1);
}

// ==================== Qt 便利方法测试 ====================

TEST(SelectionServiceTest, Qt_SelectedIdsQ_Empty)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto ids = svc.selectedIdsQ();
    EXPECT_TRUE(ids.empty());
}

TEST(SelectionServiceTest, Qt_SelectedIdsQ_WithSelection)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());

    auto ids = svc.selectedIdsQ();
    EXPECT_EQ(ids.size(), 1);
}

TEST(SelectionServiceTest, Qt_SelectEntity)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.selectEntity(QString::fromStdString(idStr));

    auto ids = svc.selectedIdsQ();
    EXPECT_EQ(ids.size(), 1);
}

TEST(SelectionServiceTest, Qt_SetSelectedEntityId)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line1 = std::make_unique<Eg::SyLine>();
    line1->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId id1 = line1->id;

    auto line2 = std::make_unique<Eg::SyLine>();
    line2->setPointVector({ Ut::Vec2d(20, 20), Ut::Vec2d(30, 30) });
    Eg::EntityId id2 = line2->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line1));
    entities.push_back(std::move(line2));
    scene.addEntities(std::move(entities));

    // 设置唯一选中
    svc.setSelectedEntityId(QString::fromStdString(std::to_string(id1)));
    auto ids = svc.selectedIdsQ();
    EXPECT_EQ(ids.size(), 1);

    // 切换唯一选中 → 应清除旧选择
    svc.setSelectedEntityId(QString::fromStdString(std::to_string(id2)));
    ids = svc.selectedIdsQ();
    EXPECT_EQ(ids.size(), 1);
}

TEST(SelectionServiceTest, Qt_SetSelectedEntityIds)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    QVector<QString> ids;
    for (int i = 0; i < 3; ++i)
    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + i, 10.0 + i) });
        ids.append(QString::fromStdString(std::to_string(line->id)));
        entities.push_back(std::move(line));
    }
    scene.addEntities(std::move(entities));

    svc.setSelectedEntityIds(ids);
    auto result = svc.selectedIdsQ();
    EXPECT_EQ(result.size(), 3);
}

TEST(SelectionServiceTest, Qt_EntityIdAt_EmptyScene)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto id = svc.entityIdAt(QPointF(50, 50));
    EXPECT_TRUE(id.isEmpty());
}

TEST(SelectionServiceTest, Qt_EntityIdAt_WithEntity)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 点查询在实体附近
    auto id = svc.entityIdAt(QPointF(5, 5), 10.0);
    // 不崩溃，返回空或 ID
    SUCCEED();
}

// ==================== 场景管理器集成测试 ====================

TEST(SelectionServiceTest, Integration_AddEntityDoesNotSelect)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    // 添加实体后不应自动选中
    auto ids = svc.selectedIdsQ();
    EXPECT_TRUE(ids.empty());
}

TEST(SelectionServiceTest, Integration_DeleteEntityClearsSelection)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());
    EXPECT_TRUE(svc.isSelected(idStr.c_str()));

    // 删除选中实体后选择应被清除
    scene.deleteEntity(scene.findSyEntityById(lineId));
    EXPECT_FALSE(svc.isSelected(idStr.c_str()));
}

TEST(SelectionServiceTest, Integration_SelectMultipleMixedTypes)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    std::string idLine, idCircle, idPoly;

    {
        auto line = std::make_unique<Eg::SyLine>();
        line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
        idLine = std::to_string(line->id);
        entities.push_back(std::move(line));
    }
    {
        auto circle = std::make_unique<Eg::SyCircle>();
        circle->basePoint = Ut::Vec2d(5, 5);
        circle->dRadius = 3.0;
        idCircle = std::to_string(circle->id);
        entities.push_back(std::move(circle));
    }
    {
        auto poly = std::make_unique<Eg::SyPolygon>();
        poly->setVertices({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 0), Ut::Vec2d(10, 10) });
        idPoly = std::to_string(poly->id);
        entities.push_back(std::move(poly));
    }
    scene.addEntities(std::move(entities));

    const char* idPtrs[] = { idLine.c_str(), idCircle.c_str(), idPoly.c_str() };
    svc.selectMultiple(idPtrs, 3);

    EXPECT_TRUE(svc.isSelected(idLine.c_str()));
    EXPECT_TRUE(svc.isSelected(idCircle.c_str()));
    EXPECT_TRUE(svc.isSelected(idPoly.c_str()));

    // 取消选中一个
    svc.deselect(idCircle.c_str());
    EXPECT_FALSE(svc.isSelected(idCircle.c_str()));
    EXPECT_TRUE(svc.isSelected(idLine.c_str()));
    EXPECT_TRUE(svc.isSelected(idPoly.c_str()));
}

// ==================== 边界条件测试 ====================

TEST(SelectionServiceTest, Boundary_SelectNonexistentId)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    svc.select("nonexistent_99999");
    // 选中不存在的 ID 不应崩溃
    SUCCEED();
}

TEST(SelectionServiceTest, Boundary_DeselectNonexistentId)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    svc.deselect("nonexistent_99999");
    SUCCEED();
}

TEST(SelectionServiceTest, Boundary_ToggleNonexistentId)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    svc.toggle("nonexistent_99999");
    SUCCEED();
}

TEST(SelectionServiceTest, Boundary_SelectEmptyString)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    svc.select("");
    EXPECT_FALSE(svc.isSelected(""));
}

TEST(SelectionServiceTest, Boundary_SelectMultipleEmpty)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    svc.selectMultiple(nullptr, 0);
    SUCCEED();
}

TEST(SelectionServiceTest, Boundary_ClearWhenEmpty)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    svc.clear();
    svc.clear();  // 重复清除
    SUCCEED();
}

TEST(SelectionServiceTest, Boundary_QuickToggleSequence)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);

    // 快速切换 10 次
    for (int i = 0; i < 10; ++i)
    {
        svc.toggle(idStr.c_str());
    }
    // 10 次切换后应回到初始状态（未选中）
    EXPECT_FALSE(svc.isSelected(idStr.c_str()));
}

TEST(SelectionServiceTest, Boundary_SelectMultipleWithZeroCount)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    svc.selectMultiple(nullptr, 0);
    SUCCEED();
}

// ==================== 场景管理器方法透传测试 ====================

TEST(SelectionServiceTest, SceneManager_GetSceneManager)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    EXPECT_EQ(svc.sceneManager(), &scene);
}

TEST(SelectionServiceTest, SceneManager_SelectReflectsInScene)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());

    // 场景管理器中的选择状态应一致
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);
}

TEST(SelectionServiceTest, SceneManager_ClearReflectsInScene)
{
    Eg::SceneManager scene;
    SelectionService svc(&scene);

    auto line = std::make_unique<Eg::SyLine>();
    line->setPointVector({ Ut::Vec2d(0, 0), Ut::Vec2d(10, 10) });
    Eg::EntityId lineId = line->id;

    std::vector<std::unique_ptr<Eg::SyEntity>> entities;
    entities.push_back(std::move(line));
    scene.addEntities(std::move(entities));

    std::string idStr = std::to_string(lineId);
    svc.select(idStr.c_str());
    EXPECT_EQ(scene.getSelectedEntityCount(), 1u);

    svc.clear();
    EXPECT_EQ(scene.getSelectedEntityCount(), 0u);
}