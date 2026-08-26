#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <QObject>
#include <QStringList>

class QAction;
class QActionGroup;
class QMenu;
class OperationBus;
class UiStateCenter;
class UiWorkbench;
class WorkbenchWindow;
struct UiFrameworkServices;
struct UiServices;
struct MenuDef;
class IUiCommandDispatcher;
class UiConfigurationManager;
class UiPanelRegistry;
class UiLayoutBuilder;
class UiShortcutRegistry;
class IShortcutSettingsModel;
struct UiConfigData;
struct MenuDispatcher;
/// 命令 UI 状态快照（选择/锁定/剪贴板/撤销栈），定义在 UI2D 命令中枢
struct CommandUiSnapshot;
#if BUILD_UI3D
/// 3D 命令 UI 状态快照，定义在 UI3D 命令中枢
struct CommandUiSnapshot3D;
#endif



class WorkbenchMenuManager : public QObject
{
    Q_OBJECT

public:
    explicit WorkbenchMenuManager(WorkbenchWindow* window, QObject* parent = nullptr);
    // MenuDispatcher 完整定义仅在 .cpp 可见，故析构在 .cpp 中定义，保证 unique_ptr 成员正确析构
    ~WorkbenchMenuManager() override;

    void setOperationBus(OperationBus* bus);
    void setStateCenter(UiStateCenter* stateCenter);
    void setFrameworkServices(const UiFrameworkServices* services);
    void setUiServices(const UiServices* services);
    void setWorkbench(UiWorkbench* workbench);
    void setViewportZoomHandler(std::function<void(const QString&)> handler);

    void buildMenus();


    /// 窗口级命令判定：这类命令刻意不进命令目录、不进命令总线，
    /// 由 MenuDispatcher 直接短路到主窗口（工作台切换 / 主题 / 语言）。
    ///
    /// 公开在此是为了单点声明：契约测试要靠它区分「真的漏接线」与「刻意走窗口级路径」，
    /// 否则测试里得再抄一份前缀名单，就成了第二处真相来源。
    static bool isWindowLevelCommand(const QString& commandId);

    void bindShortcuts();
    void rebuildAllMenus();

    /// 按命令 UI 快照刷新菜单项启用态。
    ///
    /// 菜单项的启用规则不在本类里维护 —— 一律按 property("commandId") 经
    /// CommandCatalog::menuIdForCommandId() 反查目录条目，取条目上声明的 enableRule，
    /// 再交给 Cmd::evaluateEnableRule 求值。这样菜单栏、顶部工具栏、右键菜单
    /// 共用同一份规则与同一份快照，不会出现"工具栏灰了菜单还能点"的漂移。
    ///
    /// 两类项会被跳过：
    ///   - property("commandUnavailable") 为真（命令未注册，构建期已永久禁用）
    ///   - 规则为 Always（恒可用，无需干预）
    void refreshCommandStates(const CommandUiSnapshot& snapshot);


#if BUILD_UI3D
    /// 3D 版本：规则来自 CommandCatalog3D（commandId → OperationId3D → 条目 → enableRule）。
    /// 与 2D 共用 forEachCommandAction 遍历，但绝不共用目录 —— 2D 规则不得作用于 3D 菜单项。
    void refreshCommandStates3D(const CommandUiSnapshot3D& snapshot);
#endif




    /// 所属主窗口（供命令分发器转发工作台切换等窗口级动作）
    WorkbenchWindow* workbenchWindow() const
    {
        return m_window;
    }
    /// 通过菜单配置重建菜单（JSON 驱动）
    void rebuildMenusFromConfig();

    /// 菜单/工具栏/右键菜单共用的命令分发器（本类持有生命周期）。
    ///
    /// 工具栏由 WorkbenchLayoutManager 构建，但绝不能自带一个空分发器 ——
    /// 那会让工具栏按钮全部永久禁用。此处按需创建并同步当前工作台后返回。
    IUiCommandDispatcher* commandDispatcher();

    /// 快捷键设置页模型（本类持有生命周期，可能为空——配置驱动菜单未成功构建时）。
    ///
    /// 2D/3D 设置对话框共用它：能改的就是配置驱动菜单实际绑定的那批 QAction/QShortcut，
    /// 这也是当前唯一真正生效的快捷键链路。
    IShortcutSettingsModel* shortcutSettingsModel() const;


    /// 根据工作台和命令可用性过滤配置菜单，便于测试与复用
    ///
    /// 过滤依据只有三条：菜单项的 workbenches 字段、visibilityScope、以及命令是否在
    /// 当前工作台命令目录注册。不再按命令 ID 前缀做额外白名单 —— 否则新增行业模块
    /// （laser / vision 等）在 3D 下会被静默吞掉，而 JSON 配置无法自救。
    /// 过滤后会归一化分隔符（去掉首尾与连续的空分隔线）。
    static std::vector<MenuDef> filterMenusForWorkbench(const std::vector<MenuDef>& menus,

        const QString& workbenchId,
        const std::function<bool(const QString&)>& commandAvailable,
        const QString& workbenchKind = QString());

    /// 清理全局快捷键动作（Undo/Redo），切换工作台时调用
    void clearGlobalShortcuts();

private:
    /// 绑定配置菜单状态到状态中心（checkable 状态同步）
    void bindConfiguredMenuState();
    /// 刷新配置菜单状态
    void refreshConfiguredMenuState();

    /// 遍历菜单栏与工具栏上所有带 commandId 的叶子动作。
    /// checked 同步与 enabled 同步共用这一份遍历，避免两处各写一遍递归导致覆盖范围漂移。
    void forEachCommandAction(const std::function<void(QAction*, const QString&)>& visitor) const;

    WorkbenchWindow* m_window;

    OperationBus* m_operationBus{ nullptr };
    UiStateCenter* m_stateCenter{ nullptr };
    const UiFrameworkServices* m_frameworkServices{ nullptr };
    const UiServices* m_uiServices{ nullptr };
    UiWorkbench* m_workbench{ nullptr };
    std::function<void(const QString&)> m_viewportZoomHandler;
    // 全局编辑快捷键动作（窗口级，需在切换工作台时显式清理）
    std::vector<QAction*> m_editShortcuts;

    // 客户 UI 配置不再由本类持有：统一取自 UiConfigurationManager::shared()，
    // 保证菜单 / 工具栏 / Dock / 状态栏 / 右键菜单消费同一份配置（P0-1）
    std::unique_ptr<UiLayoutBuilder> m_menuLayoutBuilder;
    std::unique_ptr<UiPanelRegistry> m_menuPanelRegistry;
    // 快捷键台账：寿命必须长于 m_menuLayoutBuilder（后者每次重建都整体替换），
    // 设置页里的快捷键模型直接指向它。
    std::unique_ptr<UiShortcutRegistry> m_shortcutRegistry;
    // 快捷键设置页模型：包装台账，随本对象存活，供 2D/3D 设置对话框复用
    std::unique_ptr<IShortcutSettingsModel> m_shortcutSettingsModel;
    // 配置菜单命令分发器：生命周期随本对象，菜单项触发回调期间必须长期有效
    std::unique_ptr<MenuDispatcher> m_dispatcher;
    // 防止每次 rebuildAllMenus 重复连接状态中心信号
    bool m_configStateBound{ false };
};
