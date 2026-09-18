#pragma once

/**
 * @file IToolbarBuilder.h
 * @brief 工具栏构建抽象（P3 边界接口）
 *
 * 消费者（工作台布局管理器）只依赖本接口 + 工厂函数，不依赖具体构建器实现
 * （UiLayoutBuilder）。这样"工具栏怎么构建"与"谁触发构建"解耦：
 * 换构建实现（不同框架/不同渲染）只需换工厂实现，消费者零改动。
 */

#include <memory>
#include <vector>

class QMainWindow;
class QToolBar;
class UiPanelRegistry;
class IUiCommandDispatcher;
struct ToolBarDef;

class IToolbarBuilder
{
public:
    virtual ~IToolbarBuilder() = default;

    /// 按配置构建工具栏
    virtual void buildToolBars(const std::vector<ToolBarDef>& toolBars) = 0;

    /// 本次构建创建的工具栏（供上层注册到布局管理器统一清理）
    virtual const std::vector<QToolBar*>& builtToolBars() const = 0;
};

/// 工厂：由具体实现提供（见 UiLayoutBuilder.cpp）
std::unique_ptr<IToolbarBuilder> createToolbarBuilder(
    QMainWindow* window, IUiCommandDispatcher* dispatcher, UiPanelRegistry* panelRegistry);
