/**
 * @file MenuFilter.cpp
 * @brief 菜单过滤工具实现
 */
#include "MenuFilter.h"

#include "UI/ClientConfig/UiClientConfigBase.h"
#include "Log/SyLogger.h"

#include <algorithm>

std::vector<MenuDef> MenuFilter::filterMenusForWorkbench(
    const std::vector<MenuDef>& menus,
    const QString& workbenchId,
    const std::function<bool(const QString&)>& commandAvailable,
    const QString& workbenchKind)
{
    std::vector<MenuDef> result;
    result.reserve(menus.size());

    for (const MenuDef& menu : menus)
    {
        if (!isVisibleForWorkbench(menu, workbenchId))
        {
            continue;
        }

        MenuDef filteredMenu = menu;
        filteredMenu.items.clear();

        for (const auto& item : menu.items)
        {
            if (std::holds_alternative<MenuActionDef>(item))
            {
                const MenuActionDef& action = std::get<MenuActionDef>(item);
                if (!isActionVisibleForWorkbench(action, workbenchId))
                {
                    continue;
                }
                // 命令可用性过滤（工作台命令目录是否注册）
                if (!action.commandId.isEmpty() && !commandAvailable(action.commandId))
                {
                    continue;
                }
                filteredMenu.items.push_back(action);
            }
            else if (std::holds_alternative<SubMenuDef>(item))
            {
                // 子菜单暂不展开过滤，保留原样（后续可递归）
                filteredMenu.items.push_back(item);
            }
            else if (std::holds_alternative<MenuItemType>(item))
            {
                const MenuItemType type = std::get<MenuItemType>(item);
                if (type == MenuItemType::Separator)
                {
                    filteredMenu.items.push_back(type);
                }
            }
        }

        // 过滤后归一化分隔符
        normalizeSeparators(filteredMenu);
        result.push_back(std::move(filteredMenu));
    }

    return result;
}

bool MenuFilter::isVisibleForWorkbench(const MenuDef& menu, const QString& workbenchId,
                                       const QString& workbenchKind)
{
    if (!menu.visible)
    {
        return false;
    }

    // workbenches 列表显式控制
    if (!menu.workbenches.isEmpty())
    {
        if (!menu.workbenches.contains(workbenchId))
        {
            return false;
        }
    }

    // visibilityScope 显式可见域
    if (!menu.visibilityScope.isEmpty())
    {
        if (menu.visibilityScope == QLatin1String("shared"))
        {
            // shared 项在所有声明该项的工作台可见
        }
        else if (menu.visibilityScope != workbenchId &&
                 menu.visibilityScope != workbenchKind)
        {
            return false;
        }
    }

    return true;
}

bool MenuFilter::isActionVisibleForWorkbench(const MenuActionDef& action,
                                             const QString& workbenchId,
                                             const QString& workbenchKind)
{
    if (!action.visible)
    {
        return false;
    }

    if (!action.workbenches.isEmpty())
    {
        if (!action.workbenches.contains(workbenchId))
        {
            return false;
        }
    }

    if (!action.visibilityScope.isEmpty())
    {
        if (action.visibilityScope == QLatin1String("shared"))
        {
            // shared 项在所有声明该项的工作台可见
        }
        else if (action.visibilityScope != workbenchId &&
                 action.visibilityScope != workbenchKind)
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
    // 先把所有 Separator 归一为统一类型
    for (auto& item : menu.items)
        {
            if (std::holds_alternative<MenuItemType>(item))
            {
                if (std::get<MenuItemType>(item) == MenuItemType::Separator)
                {
                    // 统一为 Separator（已是，无需变）
                }
            }
        }

        // 去除首尾 Separator
        while (!menu.items.empty() &&
               std::holds_alternative<MenuItemType>(menu.items.front()) &&
               std::get<MenuItemType>(menu.items.front()) == MenuItemType::Separator)
        {
            menu.items.erase(menu.items.begin());
        }
        while (!menu.items.empty() &&
               std::holds_alternative<MenuItemType>(menu.items.back()) &&
               std::get<MenuItemType>(menu.items.back()) == MenuItemType::Separator)
        {
            menu.items.pop_back();
        }

        // 折叠连续 Separator
        std::vector<std::variant<MenuActionDef, SubMenuDef, MenuItemType>> normalized;
        normalized.reserve(menu.items.size());
        bool lastWasSep = false;
        for (const auto& item : menu.items)
        {
            bool isSep = std::holds_alternative<MenuItemType>(item) &&
                std::get<MenuItemType>(item) == MenuItemType::Separator;
            if (isSep && lastWasSep)
            {
                continue; // 跳过连续
            }
            normalized.push_back(item);
            lastWasSep = isSep;
        }
        menu.items = std::move(normalized);
    }
