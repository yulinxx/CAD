/**
 * @file CommandUiWiringTests.cpp
 * @brief 命令 UI 接线回归测试 — 顶部工具栏 actionId ↔ 命令目录的解析一致性
 *
 * 背景（回归防护）：
 * 顶部工具栏此前用一张手写的 actionId → MenuActionId 映射表，表里漏了六个对齐命令，
 * 导致对齐按钮走"回退创建非托管 QAction"分支，enabled 恒为配置默认值 true——
 * 无论选中几个、是否锁定，对齐按钮始终可点。
 *
 * 现在 actionId 统一为 CommandCatalog 的命令 id，由 CommandCatalog::menuIdForCommandId 解析。
 * 本测试锁定这条解析链：ToolBarContext::Default 里的每个 id 都必须解析出有效 MenuActionId，
 * 且对齐命令必须携带"两个及以上且未锁定"的启用规则。
 */

#include <gtest/gtest.h>

#include "UI2D/Operation/CommandActionHub.h"
#include "UI2D/Operation/CommandCatalog.h"
#include "UI/Menu/MenuActionId.h"
#include "UI/ClientConfig/UiClientConfigBase.h"
#include "UI/ClientConfig/UiConfigLoader.h"
#include "UI/Workbench/WorkbenchMenuManager.h"

#if BUILD_UI3D
#include "UI3D/Operation/CommandActionHub3D.h"
#include "UI3D/Operation/CommandCatalog3D.h"
#endif


#include <QAction>
#include <QMenu>
#include <QString>
#include <QStringList>
#include <QVector>



namespace
{
    /// 与 Workbench2D::createToolbars 中 ToolBarContext::Default 注册的 actionId 列表保持一致
    QStringList defaultContextCommandIds()
    {
        return {
            QStringLiteral("edit.undo"),
            QStringLiteral("edit.redo"),
            QStringLiteral("edit.mirror_horizontal"),
            QStringLiteral("edit.mirror_vertical"),
            QStringLiteral("edit.align_left"),
            QStringLiteral("edit.align_right"),
            QStringLiteral("edit.align_center_h"),
            QStringLiteral("edit.align_top"),
            QStringLiteral("edit.align_bottom"),
            QStringLiteral("edit.align_center_v"),
            QStringLiteral("edit.select_all"),
            QStringLiteral("edit.invert_selection"),
            QStringLiteral("edit.deselect"),
            QStringLiteral("edit.copy"),
            QStringLiteral("edit.paste"),
            QStringLiteral("edit.delete"),
            QStringLiteral("edit.group"),
        };
    }
}  // namespace

TEST(CommandUiWiringTest, DefaultToolBarContextIdsAllResolveToMenuActionId)
{
    for (const QString& commandId : defaultContextCommandIds())
    {
        const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
        EXPECT_NE(static_cast<int>(menuId), 0)
            << "顶部工具栏命令 id 无法解析，按钮将退化为非托管 QAction（启用状态不再联动）: "
            << commandId.toStdString();
    }
}

TEST(CommandUiWiringTest, DefaultToolBarContextIdsAllHaveCatalogEntry)
{
    for (const QString& commandId : defaultContextCommandIds())
    {
        const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
        const CommandEntry2D* entry = CommandCatalog::findByMenuId(menuId);
        ASSERT_NE(entry, nullptr) << "命令目录缺少条目: " << commandId.toStdString();
        EXPECT_TRUE(hasSurface(entry->surfaces, CommandSurface2D::TopToolbar))
            << "命令未声明 TopToolbar 暴露面，工具栏会跳过它: " << commandId.toStdString();
    }
}

TEST(CommandUiWiringTest, AlignCommandsRequireUnlockedMultiSelection)
{
    // 用户可见规则：单选时对齐不可用，两个及以上且均未锁定才可用
    const QStringList alignIds = {
        QStringLiteral("edit.align_left"),
        QStringLiteral("edit.align_right"),
        QStringLiteral("edit.align_center_h"),
        QStringLiteral("edit.align_top"),
        QStringLiteral("edit.align_bottom"),
        QStringLiteral("edit.align_center_v"),
    };

    for (const QString& commandId : alignIds)
    {
        const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
        const CommandEntry2D* entry = CommandCatalog::findByMenuId(menuId);
        ASSERT_NE(entry, nullptr) << commandId.toStdString();
        EXPECT_EQ(entry->enableRule, CommandEnable2D::RequiresUnlockedMultiSelection)
            << commandId.toStdString();
    }
}

TEST(CommandUiWiringTest, DeleteRequiresUnlockedSelection)
{
    // 用户可见规则：选中且未锁定才允许删除
    const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(QStringLiteral("edit.delete"));
    const CommandEntry2D* entry = CommandCatalog::findByMenuId(menuId);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->enableRule, CommandEnable2D::RequiresUnlockedSelection);
}

TEST(CommandUiWiringTest, CatalogShortcutIdsResolveBackToOwnMenuId)
{
    // 目录是 actionId 的唯一真值源：凡声明了 shortcutId 的条目，
    // 反向解析必须回到自己的 menuId（防止 precise[] 之类的第二张表产生漂移）
    for (const CommandEntry2D& entry : CommandCatalog::commands())
    {
        if (!entry.shortcutId)
        {
            continue;
        }
        const QString commandId = QString::fromUtf8(entry.shortcutId);
        const UI::MenuActionId resolved = CommandCatalog::menuIdForCommandId(commandId);
        EXPECT_EQ(static_cast<int>(resolved), static_cast<int>(entry.menuId))
            << "shortcutId 反向解析漂移: " << commandId.toStdString();
    }
}

// ============================================================
// 菜单栏启用态接线
//
// 菜单项（config-driven 由 UiLayoutBuilder 建、legacy 由 addMenuAction 建）是独立于
// 命令中枢的 QAction，中枢的 refreshActionStates 触达不到。
// WorkbenchMenuManager::refreshCommandStates 按 property("commandId") →
// menuIdForCommandId → findByMenuId → enableRule 的链路统一刷新，
// 因此这条链路上任一环断裂都会让菜单项恢复"永远可点"。
// ============================================================

namespace
{
    /// base.json Edit 菜单中带启用条件的命令，与用户可见规则一一对应
    struct MenuRuleExpectation
    {
        const char* commandId;
        CommandEnable2D expectedRule;
    };

    QVector<MenuRuleExpectation> menuBarEditRuleExpectations()
    {
        return {
            { "edit.undo", CommandEnable2D::RequiresUndo },
            { "edit.redo", CommandEnable2D::RequiresRedo },
            { "edit.cut", CommandEnable2D::RequiresUnlockedSelection },
            { "edit.copy", CommandEnable2D::RequiresUnlockedSelection },
            { "edit.paste", CommandEnable2D::RequiresClipboard },
            { "edit.delete", CommandEnable2D::RequiresUnlockedSelection },
            { "edit.align_left", CommandEnable2D::RequiresUnlockedMultiSelection },
            { "edit.align_right", CommandEnable2D::RequiresUnlockedMultiSelection },
            { "edit.align_center_h", CommandEnable2D::RequiresUnlockedMultiSelection },
            { "edit.align_top", CommandEnable2D::RequiresUnlockedMultiSelection },
            { "edit.align_bottom", CommandEnable2D::RequiresUnlockedMultiSelection },
            { "edit.align_center_v", CommandEnable2D::RequiresUnlockedMultiSelection },
        };
    }
}  // namespace

TEST(CommandUiWiringTest, MenuBarEditCommandsResolveAndCarryExpectedRules)
{
    for (const MenuRuleExpectation& expectation : menuBarEditRuleExpectations())
    {
        const QString commandId = QString::fromUtf8(expectation.commandId);
        const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
        ASSERT_NE(static_cast<int>(menuId), 0)
            << "菜单项 commandId 解析落空，菜单栏刷新会跳过它（恢复永远可点）: "
            << commandId.toStdString();

        const CommandEntry2D* entry = CommandCatalog::findByMenuId(menuId);
        ASSERT_NE(entry, nullptr) << commandId.toStdString();
        EXPECT_EQ(entry->enableRule, expectation.expectedRule) << commandId.toStdString();
    }
}

TEST(CommandUiWiringTest, MenuBarEditCommandRulesAreNotAlways)
{
    // refreshCommandStates 对规则为 Always 的条目不写 enabled（避免与全局置灰互相打架）。
    // 因此这些命令一旦被误标成 Always，菜单项会静默恢复"永远可点"且不报错。
    for (const MenuRuleExpectation& expectation : menuBarEditRuleExpectations())
    {
        const QString commandId = QString::fromUtf8(expectation.commandId);
        const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
        const CommandEntry2D* entry = CommandCatalog::findByMenuId(menuId);
        ASSERT_NE(entry, nullptr) << commandId.toStdString();
        EXPECT_NE(entry->enableRule, CommandEnable2D::Always)
            << "规则为 Always 会被菜单栏刷新跳过: " << commandId.toStdString();
    }
}

TEST(CommandUiWiringTest, MenuBarEditCommandsDeclareMenuSurface)
{
    // 菜单栏项与顶部工具栏项共享同一份目录条目；条目必须声明 Menu 暴露面，
    // 否则右键菜单等按 surface 过滤的构建路径会漏掉它，与菜单栏出现不一致。
    for (const MenuRuleExpectation& expectation : menuBarEditRuleExpectations())
    {
        const QString commandId = QString::fromUtf8(expectation.commandId);
        const UI::MenuActionId menuId = CommandCatalog::menuIdForCommandId(commandId);
        const CommandEntry2D* entry = CommandCatalog::findByMenuId(menuId);
        ASSERT_NE(entry, nullptr) << commandId.toStdString();
        EXPECT_TRUE(hasSurface(entry->surfaces, CommandSurface2D::Menu)) << commandId.toStdString();
    }
}

// ============================================================
// 外部构建 QAction 树的启用态应用（配置化菜单栏 / 配置化右键菜单）
//
// 回归背景：视口右键菜单在客户配置声明了 contextMenus["canvas.2d"] 时走
// UiLayoutBuilder 构建的临时 QAction，不在中枢的动作表里，也不在菜单栏遍历范围内。
// 曾导致空选 / 选中锁定图元时右键的 Cut / Copy / Delete / Paste 仍是亮态，
// 与同名工具栏按钮的灰显直接矛盾。
// ============================================================

namespace
{
    /// 模拟 UiLayoutBuilder::bindAction / addMenuAction 的产物：只带 commandId 属性的裸 QAction
    QAction* addConfiguredAction(QMenu* menu, const char* commandId)
    {
        QAction* action = menu->addAction(QString::fromUtf8(commandId));
        action->setProperty("commandId", QString::fromUtf8(commandId));
        return action;
    }

    CommandUiSnapshot makeSnapshot(int selectionCount, bool lockedEntity, bool hasClipboard)
    {
        CommandUiSnapshot snapshot;
        snapshot.selectionCount = selectionCount;
        snapshot.hasSelection = selectionCount > 0;
        snapshot.anyLockedEntity = lockedEntity;
        snapshot.hasClipboard = hasClipboard;
        return snapshot;
    }
}  // namespace

TEST(CommandUiWiringTest, ApplySnapshotToMenu_EmptySelectionDisablesEditCommands)
{
    QMenu menu;
    QAction* cut = addConfiguredAction(&menu, "edit.cut");
    QAction* copy = addConfiguredAction(&menu, "edit.copy");
    QAction* del = addConfiguredAction(&menu, "edit.delete");
    QAction* paste = addConfiguredAction(&menu, "edit.paste");

    CommandActionHub::applySnapshotToMenu(&menu, makeSnapshot(0, false, false));

    EXPECT_FALSE(cut->isEnabled());
    EXPECT_FALSE(copy->isEnabled());
    EXPECT_FALSE(del->isEnabled());
    EXPECT_FALSE(paste->isEnabled());
}

TEST(CommandUiWiringTest, ApplySnapshotToMenu_ClipboardOnlyGatesPaste)
{
    QMenu menu;
    QAction* del = addConfiguredAction(&menu, "edit.delete");
    QAction* paste = addConfiguredAction(&menu, "edit.paste");

    // 无选中但剪贴板有内容：Paste 可用，Delete 不可用
    CommandActionHub::applySnapshotToMenu(&menu, makeSnapshot(0, false, true));

    EXPECT_TRUE(paste->isEnabled());
    EXPECT_FALSE(del->isEnabled());
}

TEST(CommandUiWiringTest, ApplySnapshotToMenu_AlignRequiresTwoUnlocked)
{
    QMenu menu;
    QAction* del = addConfiguredAction(&menu, "edit.delete");
    // 对齐在真实配置里位于 Transform/Align 子菜单，这里一并验证递归
    QMenu* alignMenu = menu.addMenu(QStringLiteral("Align"));
    QAction* alignLeft = addConfiguredAction(alignMenu, "edit.align_left");

    // 单选未锁定：删除可用，对齐不可用
    CommandActionHub::applySnapshotToMenu(&menu, makeSnapshot(1, false, false));
    EXPECT_TRUE(del->isEnabled());
    EXPECT_FALSE(alignLeft->isEnabled());

    // 两个未锁定：都可用
    CommandActionHub::applySnapshotToMenu(&menu, makeSnapshot(2, false, false));
    EXPECT_TRUE(del->isEnabled());
    EXPECT_TRUE(alignLeft->isEnabled());

    // 两个但含锁定图元：都不可用
    CommandActionHub::applySnapshotToMenu(&menu, makeSnapshot(2, true, false));
    EXPECT_FALSE(del->isEnabled());
    EXPECT_FALSE(alignLeft->isEnabled());
}

TEST(CommandUiWiringTest, ApplySnapshotToMenu_KeepsUnavailableCommandsDisabled)
{
    // 命令未注册 → UiLayoutBuilder::bindAction 构建期永久禁用并打 commandUnavailable。
    // 这是"功能没实现"，快照即便满足条件也不得把它点亮。
    QMenu menu;
    QAction* del = addConfiguredAction(&menu, "edit.delete");
    del->setEnabled(false);
    del->setProperty("commandUnavailable", true);

    CommandActionHub::applySnapshotToMenu(&menu, makeSnapshot(1, false, false));

    EXPECT_FALSE(del->isEnabled());
}

TEST(CommandUiWiringTest, ApplySnapshotToMenu_LeavesUnrelatedActionsUntouched)
{
    QMenu menu;
    // 无 commandId（分隔符替代项 / 纯 UI 项）与解析不到目录条目的 id 都应原样保留
    QAction* plain = menu.addAction(QStringLiteral("No command id"));
    QAction* unknown = addConfiguredAction(&menu, "totally.unknown.command");
    QAction* directOp = addConfiguredAction(&menu, "2d.move");  // 2d.* 前缀由目录显式返回 0

    CommandActionHub::applySnapshotToMenu(&menu, makeSnapshot(0, false, false));

    EXPECT_TRUE(plain->isEnabled());
    EXPECT_TRUE(unknown->isEnabled());
    EXPECT_TRUE(directOp->isEnabled());
}

#if BUILD_UI3D

// ============================================================
// 2D / 3D 工作台切换 — 启用态不得残留、不得串台
//
// 回归背景：
// 1) 切换工作台时 WorkbenchMenuManager::m_workbench 未同步（WorkbenchWindow 只换了
//    m_workbench），菜单派发目标、commandAvailable 判定、workbenchKind 过滤全部停留在
//    上一个工作台上 —— 3D 下点菜单会走 2D 操作总线。现由 triggerWorkbench 的
//    步骤 6b 调 setWorkbench 修正。
// 2) 3D 的配置化菜单栏此前完全没有启用态刷新（2D 侧有 refreshCommandStates，
//    3D 侧缺失），空选时 Delete / 布尔运算仍是亮态。现由
//    CommandActionHub3D::applySnapshotTo* + refreshCommandStates3D 覆盖。
//
// 两条链路共用 property("commandId") → 各自目录 → enableRule 的结构，因此
// "同一 id 在对侧目录解析不到"是防串台的关键数据契约：一个 3D 快照打到 2D 菜单树上
// 只能是 no-op，绝不能把 2D 项点亮或点灭。
// ============================================================

namespace
{
    CommandUiSnapshot3D makeSnapshot3D(int selectionCount, bool lockedEntity)
    {
        CommandUiSnapshot3D snapshot;
        snapshot.selectionCount = selectionCount;
        snapshot.hasSelection = selectionCount > 0;
        snapshot.anyLockedEntity = lockedEntity;
        return snapshot;
    }
}  // namespace

TEST(CommandUiWiringTest, Switch3D_ApplySnapshotToMenuGatesEditCommandsBySelection)
{
    // 只用无条件编译的 3D 命令：model.boolean_* / file.export_step 等挂在
    // ENABLE_GEOMODELCORE / GMC_ENABLE_STEP_IO 下，关掉时目录里没有条目，
    // 应用器会按"解析不到就不管"跳过 —— 那是正确行为，不该拿来断言灰显。
    QMenu menu;
    QAction* del = addConfiguredAction(&menu, "edit.delete");
    QAction* clearSel = addConfiguredAction(&menu, "edit.clear_selection");
    QMenu* transformMenu = menu.addMenu(QStringLiteral("Transform"));
    QAction* translate = addConfiguredAction(transformMenu, "edit.transform_translate");

    CommandActionHub3D::applySnapshotToMenu(&menu, makeSnapshot3D(0, false));
    EXPECT_FALSE(del->isEnabled());
    EXPECT_FALSE(clearSel->isEnabled());
    EXPECT_FALSE(translate->isEnabled());

    CommandActionHub3D::applySnapshotToMenu(&menu, makeSnapshot3D(2, false));
    EXPECT_TRUE(del->isEnabled());
    EXPECT_TRUE(clearSel->isEnabled());
    EXPECT_TRUE(translate->isEnabled());

    // 锁定态只拦 RequiresUnlockedSelection，不影响 RequiresSelection
    CommandActionHub3D::applySnapshotToMenu(&menu, makeSnapshot3D(2, true));
    EXPECT_FALSE(del->isEnabled());
    EXPECT_FALSE(translate->isEnabled());
    EXPECT_TRUE(clearSel->isEnabled());
}


TEST(CommandUiWiringTest, Switch_TwoDimensionalOnlyCommandsDoNotResolveIn3DCatalog)
{
    // 2D 独有命令在 3D 目录里必须解析落空，否则 3D 快照会去改 2D 菜单项的启用态
    const QStringList twoDOnlyIds = {
        QStringLiteral("edit.align_left"),
        QStringLiteral("edit.align_center_v"),
        QStringLiteral("edit.group"),
        QStringLiteral("edit.mirror_horizontal"),
    };
    for (const QString& commandId : twoDOnlyIds)
    {
        EXPECT_EQ(CommandCatalog3D::operationForCommandId(commandId), OperationId3D::None)
            << "2D 命令在 3D 目录里被解析出来了，切换后会互相改启用态: " << commandId.toStdString();
    }
}

TEST(CommandUiWiringTest, Switch_ThreeDimensionalOnlyCommandsDoNotResolveIn2DCatalog)
{
    const QStringList threeDOnlyIds = {
        QStringLiteral("model.boolean_fuse"),
        QStringLiteral("model.split_half_x"),
        QStringLiteral("file.export_stl"),
        QStringLiteral("process.generate_relief_toolpath"),
    };
    for (const QString& commandId : threeDOnlyIds)
    {
        EXPECT_EQ(static_cast<int>(CommandCatalog::menuIdForCommandId(commandId)), 0)
            << "3D 命令在 2D 目录里被解析出来了，切换后会互相改启用态: " << commandId.toStdString();
    }
}

TEST(CommandUiWiringTest, Switch_CrossWorkbenchSnapshotIsNoOpOnForeignMenu)
{
    // 行为侧对应上面两条数据契约：3D 快照打到 2D 菜单树 / 2D 快照打到 3D 菜单树都必须是 no-op。
    // 先置成 false，再用对侧快照刷新 —— 若被误认领，会被"满足条件"点亮。
    QMenu twoDMenu;
    QAction* alignLeft = addConfiguredAction(&twoDMenu, "edit.align_left");
    alignLeft->setEnabled(false);
    CommandActionHub3D::applySnapshotToMenu(&twoDMenu, makeSnapshot3D(2, false));
    EXPECT_FALSE(alignLeft->isEnabled());

    QMenu threeDMenu;
    QAction* translate = addConfiguredAction(&threeDMenu, "edit.transform_translate");
    translate->setEnabled(false);
    CommandActionHub::applySnapshotToMenu(&threeDMenu, makeSnapshot(2, false, true));
    EXPECT_FALSE(translate->isEnabled());

}

TEST(CommandUiWiringTest, Switch_SharedCommandIdsCarrySameRuleInBothCatalogs)
{
    // undo / redo / delete 在两个目录里都有条目。规则若不一致，同一份选择状态下
    // 切换工作台会看到自相矛盾的灰显（例如 2D 灰、3D 亮）。
    const QStringList sharedIds = {
        QStringLiteral("edit.undo"),
        QStringLiteral("edit.redo"),
        QStringLiteral("edit.delete"),
    };
    for (const QString& commandId : sharedIds)
    {
        const CommandEntry2D* entry2D =
            CommandCatalog::findByMenuId(CommandCatalog::menuIdForCommandId(commandId));
        const CommandEntry3D* entry3D =
            CommandCatalog3D::findByOperation(CommandCatalog3D::operationForCommandId(commandId));
        ASSERT_NE(entry2D, nullptr) << commandId.toStdString();
        ASSERT_NE(entry3D, nullptr) << commandId.toStdString();
        EXPECT_EQ(static_cast<int>(entry2D->enableRule), static_cast<int>(entry3D->enableRule))
            << "同名命令两侧启用规则不一致: " << commandId.toStdString();
    }
}

#endif  // BUILD_UI3D


// ─────────────────────────────────────────────────────────────────────────────
// 契约校验：客户 JSON 里出现的每个 commandId，都必须能在其所属工作台的命令目录里解析。
//
// 解析不出来的后果不是报错而是静默失效：UiLayoutBuilder::bindAction 不会接 lambda
// （见 isCommandRegistered 门控），按钮/菜单项照常显示但永久点不动。历史上积累了
// 几十个这样的死项，全靠人工翻 JSON 才发现。这里把它变成编译后必跑的断言。
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
    /// 一条待校验的配置引用：命令 id + 定位路径 + 生效工作台
    struct ConfigCommandRef
    {
        QString commandId;
        QString path;             // 形如 "menus/edit/edit.transform/edit.move"，失败时用于定位
        QStringList workbenches;  // 空 = JSON 未声明，按"两侧都得能解析"要求
    };

    using MenuItemVariant = std::variant<MenuActionDef, SubMenuDef, MenuItemType>;

    /// 递归收集菜单/子菜单/右键菜单里的命令项。
    /// workbenches 缺省时继承上层（JSON 里子项普遍不重复声明）。
    void collectMenuCommands(const std::vector<MenuItemVariant>& items,
        const QString& parentPath,
        const QStringList& inheritedWorkbenches,
        QVector<ConfigCommandRef>& out)
    {
        for (const MenuItemVariant& item : items)
        {
            if (const auto* action = std::get_if<MenuActionDef>(&item))
            {
                ConfigCommandRef ref;
                ref.commandId = action->commandId;
                ref.path = parentPath + QLatin1Char('/') + action->id;
                ref.workbenches = action->workbenches.isEmpty() ? inheritedWorkbenches : action->workbenches;
                out.append(ref);
            }
            else if (const auto* sub = std::get_if<SubMenuDef>(&item))
            {
                const QStringList scope = sub->workbenches.isEmpty() ? inheritedWorkbenches : sub->workbenches;
                collectMenuCommands(sub->items, parentPath + QLatin1Char('/') + sub->id, scope, out);
            }
            // MenuItemType（分隔符）不携带命令，跳过
        }
    }

    /// 把 workbenchId 单值（"2D" / "3D" / "global"）折算成 workbenches 列表
    QStringList workbenchScope(const QString& workbenchId)
    {
        if (workbenchId.compare(QLatin1String("global"), Qt::CaseInsensitive) == 0 || workbenchId.isEmpty())
        {
            return {};
        }
        return { workbenchId };
    }

    /// 收集一份配置里全部命令引用（菜单 + 工具栏 + 右键菜单 + 快捷键）
    QVector<ConfigCommandRef> collectAllCommandRefs(const UiConfigData& config)
    {
        QVector<ConfigCommandRef> refs;

        for (const MenuDef& menu : config.menus)
        {
            collectMenuCommands(menu.items, QStringLiteral("menus/") + menu.id, menu.workbenches, refs);
        }

        for (const ToolBarDef& toolBar : config.toolBars)
        {
            const QStringList scope = workbenchScope(toolBar.workbenchId);
            for (const auto& item : toolBar.items)
            {
                if (const auto* action = std::get_if<ToolBarActionDef>(&item))
                {
                    refs.append({ action->commandId,
                        QStringLiteral("toolBars/") + toolBar.id + QLatin1Char('/') + action->id,
                        scope });
                }
            }
        }

        for (const ContextMenuDef& menu : config.contextMenus)
        {
            collectMenuCommands(menu.items,
                QStringLiteral("contextMenus/") + menu.id,
                workbenchScope(menu.workbenchId),
                refs);
        }

        for (const ShortcutDef& shortcut : config.shortcuts)
        {
            // 快捷键节没有工作台字段：只要求"至少一侧能解析"，用空 scope 表达
            refs.append({ shortcut.commandId,
                QStringLiteral("shortcuts/") + shortcut.keySequence,
                QStringList() });
        }

        return refs;
    }

    bool resolvableIn2D(const QString& commandId)
    {
        return CommandCatalog::operationForCommandId(commandId) != OperationId::None;
    }

    bool resolvableIn3D(const QString& commandId)
    {
#if BUILD_UI3D
        return CommandCatalog3D::operationForCommandId(commandId) != OperationId3D::None;
#else
        Q_UNUSED(commandId);
        return true;  // 未编译 3D 时无法校验，视为通过
#endif
    }

    /// 3D 目录里被 ENABLE_GEOMODELCORE 编译门控的命令。
    /// JSON 是编译无关的，始终声明这些项；关掉 OCCT 时它们从目录里消失属于预期，
    /// 不是接线漏洞。判据跟着同一个宏走，不额外维护开关。
    bool gatedOutOf3DCatalog(const QString& commandId)
    {
#if BUILD_UI3D && !defined(ENABLE_GEOMODELCORE)
        static const QStringList kGeoModelOnly = {
            QStringLiteral("file.import_step"),
            QStringLiteral("file.export_step"),
            QStringLiteral("file.open_step"),
            QStringLiteral("file.save_brep_step"),
            QStringLiteral("file.save_brep_step_as"),
        };
        return kGeoModelOnly.contains(commandId);
#else
        Q_UNUSED(commandId);
        return false;
#endif
    }


    /// 返回该引用未满足契约的原因；满足则返回空串
    QString contractViolation(const ConfigCommandRef& ref)
    {
        // 窗口级命令（工作台切换 / 主题 / 语言）刻意不进命令目录，由 MenuDispatcher 短路，
        // 判定复用 WorkbenchMenuManager 的同一个谓词，避免测试里再抄一份名单。
        if (WorkbenchMenuManager::isWindowLevelCommand(ref.commandId))
        {
            return QString();
        }

        const bool wants2D = ref.workbenches.isEmpty() || ref.workbenches.contains(QLatin1String("2D"));
        const bool wants3D = ref.workbenches.isEmpty() || ref.workbenches.contains(QLatin1String("3D"));

        if (ref.workbenches.isEmpty())
        {
            // 未声明工作台：至少一侧能解析即可（快捷键节与 global 工具栏走这条）
            return (resolvableIn2D(ref.commandId) || resolvableIn3D(ref.commandId))
                ? QString()
                : QStringLiteral("两侧目录都解析不出");
        }

        QStringList missing;
        if (wants2D && !resolvableIn2D(ref.commandId))
        {
            missing << QStringLiteral("CommandCatalog(2D)");
        }
        if (wants3D && !resolvableIn3D(ref.commandId) && !gatedOutOf3DCatalog(ref.commandId))
        {
            missing << QStringLiteral("CommandCatalog3D");
        }
        return missing.isEmpty() ? QString() : QStringLiteral("解析不出: ") + missing.join(QLatin1String(", "));
    }

    /// 客户配置资源路径列表（configs.qrc 已把它们编进测试二进制）
    QStringList allClientConfigPaths()
    {
        return {
            QStringLiteral(":/configs/base.json"),
            QStringLiteral(":/configs/san_yi.json"),
            QStringLiteral(":/configs/client_a.json"),
            QStringLiteral(":/configs/client_b.json"),
        };
    }
}  // namespace

TEST(CommandConfigContractTest, EveryConfiguredCommandIdResolvesInItsWorkbenchCatalog)
{
    QStringList violations;
    int checked = 0;

    for (const QString& path : allClientConfigPaths())
    {
        UiConfigLoader loader(path);
        auto config = loader.load();
        ASSERT_TRUE(config.has_value()) << path.toStdString() << ": " << loader.lastError().toStdString();

        for (const ConfigCommandRef& ref : collectAllCommandRefs(*config))
        {
            ++checked;
            const QString reason = contractViolation(ref);
            if (!reason.isEmpty())
            {
                violations << QStringLiteral("%1  %2  [%3]  %4")
                                  .arg(config->meta.clientId,
                                      ref.commandId,
                                      ref.workbenches.isEmpty() ? QStringLiteral("-") : ref.workbenches.join(QLatin1Char('|')),
                                      reason)
                              + QStringLiteral("  @ ") + ref.path;
            }
        }
    }

    EXPECT_GT(checked, 0) << "一个命令引用都没收集到，说明遍历逻辑或配置加载失效";
    EXPECT_TRUE(violations.isEmpty())
        << "以下配置项的 commandId 无法解析，按钮会永久点不动（共 " << violations.size() << " 条）:\n"
        << violations.join(QLatin1Char('\n')).toStdString();
}



