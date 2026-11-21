/**
 * @file EntityIdLabelReconcilerTests.cpp
 * @brief 图元 ID 标注账本协调器回归测试
 *
 * 覆盖：
 * - 全量/增量对账的增删语义（增量路径不得扫描全场景）
 * - 视口裁剪：出视口图元不生成标签；移出盒即摘除；盒内移入即补加
 * - 预算与稳定选择：预算超限按 ID 升序截断，已有标签优先保留，平移不闪烁
 * - 隐藏图元与开关关闭时标签摘除
 * - 锚点/文本/像素偏移计算
 * - RenderWidget 字号/预算范围保护
 * - 场景切换不保留旧标签
 *
 * 无需 GL：RenderWidget 只在本用例里充当账本的消费端与开关容器，
 * 不实际提交 GPU 命令。
 */

#include <gtest/gtest.h>

#include "UI/Render/EntityIdLabelReconciler.h"
#include "RenderWidget.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/Core/SceneWriteGateway.h"
#include "Engine2D/Interaction/LayerManager.h"
#include "Engine2D/AprEntity/AprLine.h"

#include <QCoreApplication>

#include <string>
#include <vector>

namespace
{
    /// 向场景加一条可见线段，返回场景分配的持久 ID（>0，递增）
    Eg::EntityId addLine(Eg::SceneManager& scene, double x0, double y0, double x1, double y1, bool visible = true)
    {
        auto line = std::make_unique<Eg::AprLine>();
        line->setPointVector({ Ut::Vec2d(x0, y0), Ut::Vec2d(x1, y1) });
        line->setVisible(visible);
        Eg::AprEntity* added = Eg::SceneWriteGateway::addEntity(scene, line.get());
        return added ? static_cast<Eg::EntityId>(added->id) : Eg::EntityId(0);
    }

    void expectAnchorAtBboxMaxPt(const Eg::SceneManager& scene,
        const EntityIdLabelReconciler& reconciler,
        Eg::EntityId id)
    {
        const Eg::AprEntity* e = scene.findEntityById(id);
        ASSERT_NE(e, nullptr);
        const Ut::BBox2d bbox = e->getBbox();
        const Render::EntityIdLabelItem* item = reconciler.findLabel(id);
        ASSERT_NE(item, nullptr);
        EXPECT_FLOAT_EQ(item->anchorWorld.x(), static_cast<float>(bbox.maxPt.x()));
        EXPECT_FLOAT_EQ(item->anchorWorld.y(), static_cast<float>(bbox.maxPt.y()));
    }

    Ut::BBox2d boxCentered(double cx, double cy, double hw, double hh)
    {
        return Ut::BBox2d(cx - hw, cy - hh, cx + hw, cy + hh);
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
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id = addLine(scene, 0, 0, 10, 10);
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
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id1 = addLine(scene, 0, 0, 10, 10);
    Eg::EntityId id2 = addLine(scene, 100, 100, 110, 110);

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
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId visibleId = addLine(scene, 0, 0, 10, 10);
    Eg::EntityId hiddenId = addLine(scene, 100, 100, 110, 110, false);

    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(visibleId), nullptr);
    EXPECT_EQ(reconciler.findLabel(hiddenId), nullptr);
}

TEST(EntityIdLabelReconcilerTest, ViewportCullingExcludesOutOfBounds)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId nearId = addLine(scene, 0, 0, 10, 10);   // 盒内
    Eg::EntityId farId = addLine(scene, 1000, 1000, 1010, 1010);  // 盒外

    // 无盒：全场景对账，两者都有标签
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 2u);

    // 设置小视口盒：只保留盒内图元
    reconciler.setVisibleWorldBounds(boxCentered(5, 5, 100, 100));
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(nearId), nullptr);
    EXPECT_EQ(reconciler.findLabel(farId), nullptr);

    // 视口盒有效时走空间索引范围查询：候选数与显示数都只含盒内图元
    EXPECT_EQ(reconciler.stats().candidates, 1u);
    EXPECT_EQ(reconciler.stats().shown, 1u);
    EXPECT_EQ(reconciler.stats().culledByBudget, 0u);

    // 移开盒：farId 不会凭空出现
    reconciler.reconcile(&scene, true, {});
    EXPECT_EQ(reconciler.findLabel(farId), nullptr);

    // 扩大盒再全量对账：盒需同时覆盖 near(0~10) 与 far(1000~1010)，两者才都会出现。
    // boxCentered(cx, cy, hw, hh) 的 hw/hh 是半宽/半高，此处 [-95, 1105] 恰好覆盖两条线。
    reconciler.setVisibleWorldBounds(boxCentered(505, 505, 600, 600));
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 2u);
    EXPECT_NE(reconciler.findLabel(farId), nullptr);
}

TEST(EntityIdLabelReconcilerTest, BudgetLimitsAndStableSelection)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    // 无限预算先放入三个标签。ID 由进程级单例 EntityIdGenerator 分配、跨用例递增，
    // 因此这里不能硬编码 1/2/3，必须用 addLine 返回的实际 ID（按添加顺序 idA < idB < idC）。
    widget.setEntityIdLabelMaxCount(0);   // 0 = 不限
    const Eg::EntityId idA = addLine(scene, 0, 0, 10, 10);
    const Eg::EntityId idB = addLine(scene, 20, 20, 30, 30);
    const Eg::EntityId idC = addLine(scene, 40, 40, 50, 50);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 3u);

    // 紧缩预算到 1：按 ID 升序截断，保留最小的 idA
    widget.setEntityIdLabelMaxCount(1);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(idA), nullptr);
    EXPECT_EQ(reconciler.findLabel(idB), nullptr);
    EXPECT_EQ(reconciler.findLabel(idC), nullptr);

    // 再放宽到 2：已保留的 idA 优先，新增补第二个（按 ID 升序为 idB）
    widget.setEntityIdLabelMaxCount(2);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 2u);
    EXPECT_NE(reconciler.findLabel(idA), nullptr);
    EXPECT_NE(reconciler.findLabel(idB), nullptr);
    EXPECT_EQ(reconciler.findLabel(idC), nullptr);
}

TEST(EntityIdLabelReconcilerTest, BudgetKeepsExistingLabelsFirst)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    // id=2 落在盒内，id=1 在盒外（boxCentered(105,...) 覆盖 x=100 的线段）
    Eg::EntityId idFar = addLine(scene, 100, 0, 110, 10);   // id=1
    Eg::EntityId idNear = addLine(scene, 0, 0, 10, 10);      // id=2

    widget.setEntityIdLabelMaxCount(1);
    // 先只把 id=2（盒内）放进账本
    reconciler.setVisibleWorldBounds(boxCentered(5, 5, 10, 10));
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(idNear), nullptr);
    EXPECT_EQ(reconciler.findLabel(idFar), nullptr);

    // 扩大盒后两者都在范围内，预算仍为 1：已有标签 id=2 优先保留，不切换到 id=1
    reconciler.setVisibleWorldBounds(boxCentered(60, 5, 110, 10));
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(idNear), nullptr);
    EXPECT_EQ(reconciler.findLabel(idFar), nullptr);
}

TEST(EntityIdLabelReconcilerTest, IncrementalDoesNotScanFullScene)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id1 = addLine(scene, 0, 0, 10, 10);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);

    // 新增图元但不放入 dirtyIds：增量对账不得自行扫描场景发现它
    Eg::EntityId id2 = addLine(scene, 100, 100, 110, 110);
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
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id1 = addLine(scene, 0, 0, 10, 10);
    Eg::EntityId id2 = addLine(scene, 100, 100, 110, 110);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 2u);

    // 从场景删掉 id2，再对账：deletedIds 明确表达删除，id1 必须保留
    Eg::AprEntity* e2 = scene.findEntityById(id2);
    ASSERT_NE(e2, nullptr);
    Eg::SceneWriteGateway::deleteEntity(scene, e2);

    reconciler.reconcile(&scene, /*fullReconcile=*/false, {}, { id2 });
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(id1), nullptr);
    EXPECT_EQ(reconciler.findLabel(id2), nullptr);
}

TEST(EntityIdLabelReconcilerTest, IncrementalRespectsViewportBounds)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId nearId = addLine(scene, 0, 0, 10, 10);
    Eg::EntityId farId = addLine(scene, 1000, 1000, 1010, 1010);

    reconciler.setVisibleWorldBounds(boxCentered(5, 5, 10, 10));
    widget.setEntityIdLabelMaxCount(0);  // 不限
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(nearId), nullptr);

    // 修改远处图元并标记 dirty：它出视口，不补标签；同时远处新图元（也没脏）不会自动发现
    reconciler.reconcile(&scene, /*fullReconcile=*/false, { farId });
    EXPECT_EQ(reconciler.size(), 1u);
    EXPECT_EQ(reconciler.findLabel(farId), nullptr);

    // 远处图元移进盒内并标记 dirty：增量对账补上
    Eg::AprEntity* eFar = scene.findEntityById(farId);
    ASSERT_NE(eFar, nullptr);
    auto* farLine = dynamic_cast<Eg::AprLine*>(eFar);
    ASSERT_NE(farLine, nullptr);
    farLine->setPointVector({ Ut::Vec2d(2, 2), Ut::Vec2d(6, 6) });

    reconciler.reconcile(&scene, /*fullReconcile=*/false, { farId });
    ASSERT_EQ(reconciler.size(), 2u);
    EXPECT_NE(reconciler.findLabel(farId), nullptr);
}

TEST(EntityIdLabelReconcilerTest, FullReconcileRemovesDeletedFromScene)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id1 = addLine(scene, 0, 0, 10, 10);
    Eg::EntityId id2 = addLine(scene, 100, 100, 110, 110);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 2u);

    Eg::AprEntity* e2 = scene.findEntityById(id2);
    ASSERT_NE(e2, nullptr);
    Eg::SceneWriteGateway::deleteEntity(scene, e2);

    // 全量对账以场景为真源：不在场景里的 id2 条目会被摘除
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);
    EXPECT_NE(reconciler.findLabel(id1), nullptr);
    EXPECT_EQ(reconciler.findLabel(id2), nullptr);
}

TEST(EntityIdLabelReconcilerTest, ResetClearsLedgerAndViewportBox)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    Eg::EntityId id = addLine(scene, 0, 0, 10, 10);
    reconciler.reconcile(&scene, true, {});
    ASSERT_EQ(reconciler.size(), 1u);

    reconciler.setVisibleWorldBounds(boxCentered(5, 5, 100, 100));
    reconciler.reset();
    EXPECT_TRUE(reconciler.empty());
    EXPECT_EQ(reconciler.stats().candidates, 0u);
    EXPECT_EQ(reconciler.findLabel(id), nullptr);
}

TEST(EntityIdLabelReconcilerTest, InvalidIdsAreNeverLabeled)
{
    Eg::SceneManager scene;
    // 镜像生产环境：LayerManager 在图元入场时把它绑定到可见的默认图层 0。
    // 缺少它则新图元 pLayer 为 null，会被 buildLabel 的图层可见性判据过滤掉。
    LayerManager layers(&scene);
    RenderWidget widget;
    EntityIdLabelReconciler reconciler(&widget);
    widget.setShowEntityIdLabels(true);

    // 0 或负数 ID（临时/合成号段）绝不进入账本：需要合成持久 ID 的路径不会
    // 从 SceneManager 遍历，因此直接断言 findLabel 为空
    EXPECT_EQ(reconciler.findLabel(0), nullptr);
    EXPECT_EQ(reconciler.findLabel(-1), nullptr);
    EXPECT_EQ(reconciler.findLabel(std::numeric_limits<Eg::EntityId>::max()), nullptr);
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

TEST(RenderWidgetTest, EntityIdLabelMaxCountClamped)
{
    RenderWidget widget;
    EXPECT_EQ(widget.entityIdLabelMaxCount(), 5000);

    widget.setEntityIdLabelMaxCount(0);
    EXPECT_EQ(widget.entityIdLabelMaxCount(), 0);   // 不限
    widget.setEntityIdLabelMaxCount(-5);
    EXPECT_EQ(widget.entityIdLabelMaxCount(), 0);   // 负值也视为不限
    widget.setEntityIdLabelMaxCount(100000);
    EXPECT_EQ(widget.entityIdLabelMaxCount(), 50000);
    widget.setEntityIdLabelMaxCount(2);
    EXPECT_EQ(widget.entityIdLabelMaxCount(), 2);   // 小预算可用于单元测试
    widget.setEntityIdLabelMaxCount(5000);
    EXPECT_EQ(widget.entityIdLabelMaxCount(), 5000);
}
