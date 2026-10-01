/**
 * @file MenuFilter.h
 * @brief 菜单过滤工具类（从 WorkbenchMenuManager 拆分）
 *
 * 职责：按工作台 ID / visibilityScope / 命令可用性过滤菜单配置。
 * 统一供菜单栏、工具栏、右键菜单三处复用，消除重复逻辑。
 */
#pragma once

#include <QString>
#include <functional>
#include <vector>

struct MenuDef;

class MenuFilter
{
public:
    /// 过滤菜单列表
    /// @param menus 原始菜单列表
    /// @param workbenchId 目标工作台 ID（"2D"/"3D"）
    /// @param commandAvailable 命令可用性判定函数（commandId -> bool）
    /// @param workbenchKind 可选：工作台类型（用于 visibilityScope=shared 判定）
    /// @return 过滤后的菜单列表（已归一化分隔符：去首尾/连续空分隔）
    static std::vector<MenuDef> filterMenusForWorkbench(
        const std::vector<MenuDef>& menus,
        const QString& workbenchId,
        const std::function<bool(const QString&)>& commandAvailable,
        const QString& workbenchKind = QString());

    /// 单条菜单项是否对指定工作台可见
    static bool isVisibleForWorkbench(const MenuDef& menu, const QString& workbenchId,
                                      const QString& workbenchKind = QString());

    /// 单个动作项是否对指定工作台可见
    static bool isActionVisibleForWorkbench(const struct MenuActionDef& action,
                                            const QString& workbenchId,
                                            const QString& workbenchKind = QString());

    /// 归一化分隔符：去首尾/连续空分隔
    static void normalizeSeparators(std::vector<MenuDef>& menus);
    static void normalizeSeparators(MenuDef& menu);
};