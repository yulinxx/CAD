/**
 * @file MenuFilter.cpp
 * @brief 菜单过滤工具实现（从 WorkbenchMenuManager 完整移植）
 *
 * 职责：按工作台 ID / visibilityScope / 命令可用性 / 动态段保留过滤菜单配置。
 * 语义与 WorkbenchMenuManager::filterMenusForWorkbench 完全一致：
 *   - 递归过滤子菜单（SubMenuDef 内部再递归）
 *   - workbenches 列表用 Qt::CaseInsensitive 匹配
 *   - visibilityScope 按 workbenchKind 判定（3D/shared 或 2D/shared）
 *   - dynamicSections 非空的子菜单即使静态条目为空也保留
 *   - 过滤后归一化分隔符（去首尾/连续空分隔）
 *   - 空菜单裁剪（除非 dynamicSections 非空）
 */
#include "MenuFilter.h"

#include "UI/ClientConfig/UiClientConfigBase.h"

#include <QString>
#include <algorithm>

namespace
{
    bool commandEnabledForWorkbench(const QStringList& workbenches, const QString& workbenchId)
    {
        if (workbenches.isEmpty())
        {
            return true;
        }
        for (const auto& wb : workbenches)
        {
            if (wb.compare(workbenchId, Qt::CaseInsensitive) == 0)
            {
                return true;
            }
        }
        return false;
    }
}  // namespace

std::vector<MenuDef> MenuFilter::filterMenusForWorkbench(
    const std::vector<MenuDef>& menus,
    const QString& workbenchId,
    const std::function<bool(const QString&)>& commandAvailable,
    const QString& workbenchKind)
{
    // 分隔符归一化：JSON 里的分隔符是按"全部菜单项都在"排版的，
    // 过滤掉不属于当前工作台的动作后会留下开头/结尾/连续的空分隔线，
    // 这会让 3D 菜单看起来像一堆断裂的空行。这里在数据层收敛，构建层无需关心。
    const auto normalizeSeparators =
        [](std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>>& items) {
            const auto isSeparator =
                [](const std::variant<MenuActionDef, SubMenuDef, MenuItemType>& item) {
                    return std::holds_alternative<MenuItemType>(item) &&
                        std::get<MenuItemType>(item) == MenuItemType::Separator;
                };
            std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> normalized;

            normalized.reserve(items.size());
            for (const auto& item : items)
            {
                if (isSeparator(item) && (normalized.empty() || isSeparator(normalized.back())))
                {
                    continue;
                }
                normalized.push_back(item);
            }
            while (!normalized.empty() && isSeparator(normalized.back()))
            {
                normalized.pop_back();
            }
            items.swap(normalized);
        };

    const auto visibilityAllowed = [&](const QString& scope) {
        if (scope.isEmpty())
        {
            return true;
        }
        if (workbenchKind.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
        {
            return scope.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0 ||
                scope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0;
        }
        return scope.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0 ||
            scope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0;
    };

    std::function<bool(const MenuActionDef&, MenuActionDef&)> filterAction =
        [&](const MenuActionDef& action, MenuActionDef& outAction) -> bool {
        if (!action.visible || !commandEnabledForWorkbench(action.workbenches, workbenchId))
        {
            return false;
        }
        if (!visibilityAllowed(action.visibilityScope))
        {
            return false;
        }
        if (!commandAvailable(action.commandId))
        {
            return false;
        }

        outAction = action;
        return true;
    };

    std::function<bool(const SubMenuDef&, SubMenuDef&)> filterSubMenu =
        [&](const SubMenuDef& sub, SubMenuDef& outSub) -> bool {
        if (!sub.visible || !commandEnabledForWorkbench(sub.workbenches, workbenchId))
        {
            return false;
        }
        if (!visibilityAllowed(sub.visibilityScope))
        {
            return false;
        }
        outSub = sub;
        outSub.items.clear();
        for (const auto& subItem : sub.items)
        {
            if (std::holds_alternative<MenuActionDef>(subItem))
            {
                MenuActionDef filteredAction;
                if (filterAction(std::get<MenuActionDef>(subItem), filteredAction))
                {
                    outSub.items.push_back(filteredAction);
                }
            }
            else if (std::holds_alternative<SubMenuDef>(subItem))
            {
                SubMenuDef filteredSub;
                if (filterSubMenu(std::get<SubMenuDef>(subItem), filteredSub))
                {
                    outSub.items.push_back(filteredSub);
                }
            }
            else if (std::holds_alternative<MenuItemType>(subItem))
            {
                outSub.items.push_back(subItem);
            }
        }
        normalizeSeparators(outSub.items);
        // 声明了 dynamicSections 的子菜单不能因为"静态条目为空"被裁掉 ——
        // 这类子菜单的条目本来就在运行时才生成（File ▸ Recent Files 是纯动态的，
        // 静态条目一个都没有）。裁掉的话 UiLayoutBuilder 根本看不到它，动态段永远填不进去。
        return !outSub.items.empty() || !outSub.dynamicSections.isEmpty();
    };

    std::vector<MenuDef> filteredMenus;
    filteredMenus.reserve(menus.size());
    for (const auto& menu : menus)
    {
        if (!menu.visible || !commandEnabledForWorkbench(menu.workbenches, workbenchId))
        {
            continue;
        }
        if (!visibilityAllowed(menu.visibilityScope))
        {
            continue;
        }
        MenuDef menuCopy = menu;
        menuCopy.items.clear();
        for (const auto& item : menu.items)
        {
            if (std::holds_alternative<MenuActionDef>(item))
            {
                MenuActionDef filteredAction;
                if (filterAction(std::get<MenuActionDef>(item), filteredAction))
                {
                    menuCopy.items.push_back(filteredAction);
                }
            }
            else if (std::holds_alternative<SubMenuDef>(item))
            {
                SubMenuDef filteredSub;
                if (filterSubMenu(std::get<SubMenuDef>(item), filteredSub))
                {
                    menuCopy.items.push_back(filteredSub);
                }
            }
            else if (std::holds_alternative<MenuItemType>(item))
            {
                menuCopy.items.push_back(item);
            }
        }
        normalizeSeparators(menuCopy.items);
        if (!menuCopy.items.empty())
        {
            filteredMenus.push_back(menuCopy);
        }
    }
    return filteredMenus;
}

bool MenuFilter::isVisibleForWorkbench(const MenuDef& menu, const QString& workbenchId,
                                       const QString& workbenchKind)
{
    if (!menu.visible)
    {
        return false;
    }
    if (!commandEnabledForWorkbench(menu.workbenches, workbenchId))
    {
        return false;
    }
    if (!menu.visibilityScope.isEmpty())
    {
        if (workbenchKind.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
        {
            if (!(menu.visibilityScope.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0 ||
                    menu.visibilityScope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0))
            {
                return false;
            }
        }
        else if (!(menu.visibilityScope.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0 ||
                      menu.visibilityScope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0))
        {
            return false;
        }
    }
    return true;
}

bool MenuFilter::isActionVisibleForWorkbench(const MenuActionDef& action, const QString& workbenchId,
                                             const QString& workbenchKind)
{
    if (!action.visible)
    {
        return false;
    }
    if (!commandEnabledForWorkbench(action.workbenches, workbenchId))
    {
        return false;
    }
    if (!action.visibilityScope.isEmpty())
    {
        if (workbenchKind.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0)
        {
            if (!(action.visibilityScope.compare(QStringLiteral("3D"), Qt::CaseInsensitive) == 0 ||
                    action.visibilityScope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0))
            {
                return false;
            }
        }
        else if (!(action.visibilityScope.compare(QStringLiteral("2D"), Qt::CaseInsensitive) == 0 ||
                      action.visibilityScope.compare(QStringLiteral("shared"), Qt::CaseInsensitive) == 0))
        {
            return false;
        }
    }
    return true;
}

void MenuFilter::normalizeSeparators(std::vector<MenuDef>& menus)
{
    for (MenuDef& menu : menus)
    {
        normalizeSeparators(menu);
    }
}

void MenuFilter::normalizeSeparators(MenuDef& menu)
{
    const auto isSeparator = [](const std::variant<MenuActionDef, SubMenuDef, MenuItemType>& item) {
        return std::holds_alternative<MenuItemType>(item) &&
            std::get<MenuItemType>(item) == MenuItemType::Separator;
    };
    std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> normalized;

    normalized.reserve(menu.items.size());
    for (const auto& item : menu.items)
    {
        if (isSeparator(item) && (normalized.empty() || isSeparator(normalized.back())))
        {
            continue;
        }
        normalized.push_back(item);
    }
    while (!normalized.empty() && isSeparator(normalized.back()))
    {
        normalized.pop_back();
    }
    menu.items = std::move(normalized);
}
