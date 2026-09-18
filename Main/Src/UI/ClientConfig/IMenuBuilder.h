#pragma once

/**
 * @file IMenuBuilder.h
 * @brief 菜单/快捷键构建抽象（P3 边界接口）
 *
 * 消费者（WorkbenchMenuManager）只依赖本接口 + 工厂，不依赖具体构建器
 * （UiLayoutBuilder）。这样"菜单怎么构建"与"谁触发构建"解耦。
 */

#include <memory>
#include <vector>

class QMainWindow;
class UiPanelRegistry;
class UiShortcutRegistry;
class IUiCommandDispatcher;
struct MenuDef;
struct ShortcutDef;

class IMenuBuilder
{
public:
    virtual ~IMenuBuilder() = default;

    /// 清空上一次构建的布局（菜单重建前调用）
    virtual void clearBuiltLayout() = 0;

    /// 挂入快捷键台账（必须在 buildMenus/buildShortcuts 之前设置）
    virtual void setShortcutRegistry(UiShortcutRegistry* registry) = 0;

    /// 按配置构建菜单
    virtual void buildMenus(const std::vector<MenuDef>& menus) = 0;

    /// 按配置构建快捷键
    virtual void buildShortcuts(const std::vector<ShortcutDef>& shortcuts) = 0;
};

/// 工厂：由具体实现提供（见 UiLayoutBuilder.cpp）
std::unique_ptr<IMenuBuilder> createMenuBuilder(
    QMainWindow* window, IUiCommandDispatcher* dispatcher, UiPanelRegistry* panelRegistry);
