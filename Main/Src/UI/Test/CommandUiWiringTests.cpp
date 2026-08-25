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

#include "UI2D/Operation/CommandCatalog.h"
#include "UI/Menu/MenuActionId.h"

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

