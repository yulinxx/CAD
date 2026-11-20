/**
 * @file EntityIdLabelReconcilerTests.cpp
 * @brief 图元 ID 标注账本协调器回归测试
 *
 * 覆盖：
 * - 全量/增量对账的增删语义（增量路径不得扫描全场景）
 * - 隐藏图元与开关关闭时标签摘除
 * - 锚点/文本/像素偏移计算
 * - RenderWidget 字号范围保护
 *
 * 无需 GL：RenderWidget 只在本用例里充当账本的消费端与开关容器，
 * 不实际提交 GPU 命令。
 */

#include <gtest/gtest.h>

#include "UI/Render/EntityIdLabelReconciler.h"
#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/AprEntity/AprLine.h"

#include <QCoreApplication>

#include <string>
#include <vector>

namespace
{
    /// 向场景加一条可见线段，返回场景分配的持久 ID（>0）
    Eg::EntityId addLine(Eg::SceneManager& scene, double dx = 0.0, bool visible = true)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(10.0 + dx, 10.0 + dx) });
        line->setVisible(visible);
        Eg::AprEntity* added = Eg::SceneWriteGateway::addEntity(scene, line.get());
        return added ? static_cast<Eg::EntityId>(added->id) : Eg::EntityId(0);
    }

    void expectAnchorAtBboxMaxPt(const Eg::SceneManager& scene,
        const EntityIdLabelReconciler& reconciler,
        Eg::EntityId id)
    {
        const Eg::AprEntity* e = scene.findAprEntityById(id);
        ASSERT_NE(e, nullptr);
        const Ut::BBox2d bbox = e->getBbox();
        const Render::EntityIdLabelItem* item = reconciler.findLabel(id);
        ASSERT_NE(item, nullptr);
        EXPECT_FLOAT_EQ(item->anchorWorld.x(), static_cast<float>(bbox.maxPt.x()));
        EXPECT_FLOAT_EQ(item->anchorWorld.y(), static_cast<float>(bbox.maxPt.y()));
    }
}  // namespace

TEST(EntityIdLabelReconcilerTest, ConstructWithoutWidget)
{
    EntityIdLabelReconciler reconciler(nullptr);
    EXPECT_TRUE(reconciler.empty());
    EXPECT_EQ(reconciler.size(), 0u);
    EXPECT_EQ(reconciler.findLabel(123), nullptr);
}

TEST(EntityIdLabelReconcilerTest, SwitchOffClearsLabelsWithoutScanningScene)
{
    Eg::SceneManager scene;
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id = addLine(scene);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);

    // 开关关闭后直接清空账本，不扫描场景；下一帧标签即消失
    widget.setShowEntityIdLabels(false);
    reconciler.reconcile(&scene, true, {});
    EXPECT_TRUE(reconciler.empty());
}

TEST(EntityIdLabelReconcilerTest, FullReconcilePopulatesLabels)
{
    Eg::SceneManager scene;
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id1 = addLine(scene);
    Eg::EntityId id2 = addLine(scene, 100.0);

    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 2u);

    const Render::EntityIdLabelItem* item1 = reconciler.findLabel(id1);
    ASSERT_NE(item1, nullptr);
    EXPECT_EQ(item1->text, std::to_string(id1));
    EXPECT_FLOAT_EQ(item1->offsetPx.x(), 4.0f);
    EXPECT_FLOAT_EQ(item1->offsetPx.y(), -4.0f);

    expectAnchorAtBboxMaxPt(scene, reconciler, id1);
    expectAnchorAtBboxMaxPt(scene, reconciler, id2);
}

TEST(EntityIdLabelReconcilerTest, HiddenEntityNotLabeled)
{
    Eg::SceneManager scene;
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId visibleId = addLine(scene);
    Eg::EntityId hiddenId = addLine(scene, 10.0, false);

    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(visibleId), nullptr);
    EXPECT_EQ(reconciler.findLabel(hiddenId), nullptr);
}

TEST(EntityIdLabelReconcilerTest, IncrementalDoesNotScanFullScene)
{
    Eg::SceneManager scene;
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id1 = addLine(scene);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);

    // 新增图元但不放入 dirtyIds：增量对账不得自行扫描场景发现它
    Eg::EntityId id2 = addLine(scene, 100.0);
    reconciler.reconcile(&scene, /*fullReconcile=*/false, {});
    EXPECT_EQ(reconciler.size(), 1u);
    EXPECT_EQ(reconciler.findLabel(id2), nullptr);

    // 把它放进 dirtyIds 再对账：应增量补上，其余图元不动
    reconciler.reconcile(&scene, /*fullReconcile=*/false, { id2 });
    ASSERT_EQ(reconciler.size(), 2u);
    EXPECT_NE(reconciler.findLabel(id2), nullptr);
    EXPECT_NE(reconciler.findLabel(id1), nullptr);
}

TEST(EntityIdLabelReconcilerTest, IncrementalRemoveDeleted)
{
    Eg::SceneManager scene;
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id1 = addLine(scene);
    Eg::EntityId id2 = addLine(scene, 100.0);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 2u);

    // 只把 id2 放进 deletedIds：id1 必须保留
    reconciler.reconcile(&scene, /*fullReconcile=*/false, {}, { id2 });
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(id1), nullptr);
    EXPECT_EQ(reconciler.findLabel(id2), nullptr);
}

TEST(EntityIdLabelReconcilerTest, UpdateDirtyMovesAnchor)
{
    Eg::SceneManager scene;
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id = addLine(scene, 0.0);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    const Render::EntityIdLabelItem* before = reconciler.findLabel(id);
    ASSERT_NE(before, nullptr);
    const float beforeX = before->anchorWorld.x();

    // 修改图元端点并标记 dirty：增量对账应重算锚点
    Eg::AprEntity* e = scene.findAprEntityById(id);
    ASSERT_NE(e, nullptr);
    auto* line = dynamic_cast<Eg::AprLine*>(e);
    ASSERT_NE(line, nullptr);
    line->setPointVector({ Ut::Vec2d(0.0, 0.0), Ut::Vec2d(999.0, 999.0) });

    reconciler.reconcile(&scene, /*fullReconcile=*/false, { id });
    const Render::EntityIdLabelItem* after = reconciler.findLabel(id);
    ASSERT_NE(after, nullptr);
    EXPECT_NE(after->anchorWorld.x(), beforeX);
    EXPECT_FLOAT_EQ(after->anchorWorld.x(), static_cast<float>(e->getBbox().maxPt.x()));
}

TEST(RenderWidgetTest, EntityIdLabelFontSizeClamped)
{
    RenderWidget widget;
    widget.setEntityIdLabelFontSize(0);
    EXPECT_EQ(widget.entityIdLabelFontSize(), 6);
    widget.setEntityIdLabelFontSize(-5);
    EXPECT_EQ(widget.entityIdLabelFontSize(), 6);
    widget.setEntityIdLabelFontSize(100);
    EXPECT_EQ(widget.entityIdLabelFontSize(), 32);
    widget.setEntityIdLabelFontSize(14);
    EXPECT_EQ(widget.entityIdLabelFontSize(), 14);
}
