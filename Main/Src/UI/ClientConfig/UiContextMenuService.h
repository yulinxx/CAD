#pragma once

/**
 * @file UiContextMenuService.h
 * @brief 配置驱动的右键菜单服务（P0-2b）+ 全仓唯一的菜单动态段注册表
 *
 * 背景：
 *   右键菜单历史上完全由 C++ 硬编码组装（CommandActionHub::populateContextMenu /
 *   CommandActionHub3D::populateContextMenu），是 UI 定制体系里最后一块没有纳入
 *   JSON 的表面。客户想调整右键项就必须改 C++。
 *
 * 设计：
 *   静态部分 —— 由客户 JSON 的 contextMenus 节声明，走 UiLayoutBuilder 构建，
 *               与顶部菜单共享命令绑定、图标解析、授权门控行为。
 *   动态部分 —— 菜单里有内容天生无法静态描述（右键的「设为当前图层 / 移动到图层…」
 *               需要按运行时图层列表生成；File ▸ Recent Files 需要按最近文件列表生成）。
 *               这类内容由 C++ 侧注册「动态段提供者」，JSON 只用 dynamicSections
 *               声明插入哪些段、按什么顺序。这样既保住了配置化，也不必把运行时数据塞进 JSON。
 *
 * 段注册表是全仓唯一的一份，两个菜单表面共用：
 *   - 右键菜单：本类 buildMenu() 里追加（ContextMenuDef::dynamicSections）
 *   - 主菜单子菜单：UiLayoutBuilder 里追加（SubMenuDef::dynamicSections）
 *   两边都调用 fillDynamicSections()，不要各写一份追加逻辑。
 *
 * 使用约定：
 *   1. 启动时调用 registerDynamicSection() 注册动态段。
 *   2. 右键事件里调用 buildMenu()；返回 nullptr 表示该 id 没有可用配置，
 *      调用方应回退到内建的 populateContextMenu 路径。
 *   3. 返回的 QMenu 归调用方所有，exec 之后需 deleteLater()。
 */

#include <QMap>
#include <QString>
#include <QStringList>
#include <functional>

class QMenu;
class QWidget;
class IUiCommandDispatcher;
struct UiConfigData;

/// 动态段填充回调：向已构建的菜单追加运行时生成的条目
/// 右键菜单与主菜单子菜单共用此类型，故不叫 ContextMenu*
using UiMenuSectionFiller = std::function<void(QMenu* menu)>;

/// 配置驱动的右键菜单服务 + 菜单动态段注册表（进程级单例）
class UiContextMenuService
{
public:
    static UiContextMenuService& instance();

    /// 注册动态段提供者
    /// @param sectionId 与 JSON 里 dynamicSections 中的字符串对应
    ///        （contextMenus[].dynamicSections 或 menus[].items[].dynamicSections）
    /// @param filler 填充回调；同 id 重复注册会覆盖
    void registerDynamicSection(const QString& sectionId, UiMenuSectionFiller filler);

    /// 注销动态段提供者
    /// 本服务是进程级单例，而 filler 闭包普遍捕获工作台裸指针。工作台
    /// deactivate() 时必须注销，否则闭包会跨工作台存活：另一侧的 JSON 只要声明
    /// 同名 dynamicSections，右键就会打进已失效的工作台对象。
    /// @param sectionId 与注册时一致；未注册时静默返回
    void unregisterDynamicSection(const QString& sectionId);

    /// 按声明顺序把动态段追加到 menu 末尾
    ///
    /// 右键菜单与主菜单子菜单共用这一份追加逻辑：未注册的段只告警并跳过，
    /// 不中断其余段，避免一个客户配置写错就整块菜单消失。
    /// @param ownerId 仅用于日志定位（右键菜单 id 或子菜单 id）
    /// @return 实际追加的条目数
    int fillDynamicSections(QMenu* menu, const QStringList& sectionIds, const QString& ownerId);

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

    QMap<QString, UiMenuSectionFiller> m_sections;
};
