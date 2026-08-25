#pragma once

/**
 * @file UiContextMenuService.h
 * @brief 配置驱动的右键菜单服务（P0-2b）
 *
 * 背景：
 *   右键菜单历史上完全由 C++ 硬编码组装（CommandActionHub::populateContextMenu /
 *   CommandActionHub3D::populateContextMenu），是 UI 定制体系里最后一块没有纳入
 *   JSON 的表面。客户想调整右键项就必须改 C++。
 *
 * 设计：
 *   静态部分 —— 由客户 JSON 的 contextMenus 节声明，走 UiLayoutBuilder 构建，
 *               与顶部菜单共享命令绑定、图标解析、授权门控行为。
 *   动态部分 —— 右键菜单里有内容天生无法静态描述（典型是「设为当前图层 /
 *               移动到图层…」，需要按运行时图层列表生成）。这类内容由 C++ 侧
 *               注册「动态段提供者」，JSON 只用 dynamicSections 声明插入哪些段、
 *               按什么顺序。这样既保住了配置化，也不必把运行时数据塞进 JSON。
 *
 * 使用约定：
 *   1. 启动时调用 registerDynamicSection() 注册动态段。
 *   2. 右键事件里调用 buildMenu()；返回 nullptr 表示该 id 没有可用配置，
 *      调用方应回退到内建的 populateContextMenu 路径。
 *   3. 返回的 QMenu 归调用方所有，exec 之后需 deleteLater()。
 */

#include <QMap>
#include <QString>
#include <functional>

class QMenu;
class QWidget;
class IUiCommandDispatcher;
struct UiConfigData;

/// 动态段填充回调：向已构建的菜单追加运行时生成的条目
using UiContextMenuSectionFiller = std::function<void(QMenu* menu)>;

/// 配置驱动的右键菜单服务（进程级单例）
class UiContextMenuService
{
public:
    static UiContextMenuService& instance();

    /// 注册动态段提供者
    /// @param sectionId 与 JSON contextMenus[].dynamicSections 中的字符串对应
    /// @param filler 填充回调；同 id 重复注册会覆盖
    void registerDynamicSection(const QString& sectionId, UiContextMenuSectionFiller filler);

    /// 按配置构建右键菜单
    /// @param config 当前客户配置（通常取自 WorkbenchLayoutManager::configManager()）
    /// @param contextMenuId 菜单 ID，例如 "canvas.2d"
    /// @param dispatcher 命令分发器（复用工作台的 MenuDispatcher，保证命令路径一致）
    /// @param parent 菜单父对象
    /// @return 构建好的菜单；配置缺失或无可用条目时返回 nullptr（调用方据此回退）
    QMenu* buildMenu(const UiConfigData* config,
        const QString& contextMenuId,
        IUiCommandDispatcher* dispatcher,
        QWidget* parent);

    /// 指定 id 是否存在配置项（供调用方提前决定走配置路径还是内建路径）
    static bool hasConfigFor(const UiConfigData* config, const QString& contextMenuId);

    /// 清空已注册的动态段（仅测试使用）
    void resetForTest();

private:
    UiContextMenuService() = default;

    QMap<QString, UiContextMenuSectionFiller> m_sections;
};
