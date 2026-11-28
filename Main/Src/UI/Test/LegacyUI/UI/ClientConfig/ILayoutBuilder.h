#pragma once

/**
 * @file ILayoutBuilder.h
 * @brief 布局构建抽象（P3 边界接口）：工具栏 + Dock + 状态栏
 *
 * 消费者（工作台布局管理器）只依赖本接口 + 工厂，不依赖具体构建器
 * （UiLayoutBuilder）。这样"布局怎么构建"与"谁触发构建"解耦。
 */

#include "IToolbarBuilder.h"

#include <memory>
#include <vector>

class QWidget;
struct DockDef;
struct StatusBarDef;

class ILayoutBuilder : public IToolbarBuilder
{
public:
    virtual ~ILayoutBuilder() = default;

    /// 按配置构建 Dock
    virtual void buildDocks(const std::vector<DockDef>& docks) = 0;

    /// 本次构建创建的 Dock widget（供上层注册到布局管理器统一清理）
    virtual const std::vector<QWidget*>& builtDocks() const = 0;

    /// 按配置构建状态栏槽位
    virtual void buildStatusBar(const StatusBarDef& statusBar) = 0;

    /// 本次构建挂入状态栏的槽位控件（供上层统一清理）
    virtual const std::vector<QWidget*>& builtStatusBarSlots() const = 0;
};

/// 工厂：由具体实现提供（见 UiLayoutBuilder.cpp）
std::unique_ptr<ILayoutBuilder> createLayoutBuilder(
    QMainWindow* window, IUiCommandDispatcher* dispatcher, UiPanelRegistry* panelRegistry);
