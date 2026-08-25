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
struct UiConfigData;
struct MenuDispatcher;
/// 命令 UI 状态快照（选择/锁定/剪贴板/撤销栈），定义在 UI2D 命令中枢
struct CommandUiSnapshot;


using WorkbenchFactory = std::function<UiWorkbench*(const QString& workbenchId)>;

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
    void setWorkbenchFactory(WorkbenchFactory factory);
    void setViewportZoomHandler(std::function<void(const QString&)> handler);
    /// 设置 TestView 菜单回调（打开独立预览渲染窗口）
    void setTestViewHandler(std::function<void()> handler);

    void buildMenus();
    void buildThemeMenu();
    void bindMenuCommands();
    void bindShortcuts();
    void rebuildAllMenus();
    /// 算法执行 / 仅展示模式下，全局启用或禁用全部菜单交互（含子菜单项）。
    /// 例如算法后台运行时调用 setAllMenusEnabled(false) 使整栏置灰仅展示，完成后传 true 恢复。
    void setAllMenusEnabled(bool enabled);

    /// 按命令 UI 快照刷新菜单项启用态。
    ///
    /// 菜单项的启用规则不在本类里维护 —— 一律按 property("commandId") 经
    /// CommandCatalog::menuIdForCommandId() 反查目录条目，取条目上声明的 enableRule，
    /// 再交给 Cmd::evaluateEnableRule 求值。这样菜单栏、顶部工具栏、右键菜单
    /// 共用同一份规则与同一份快照，不会出现"工具栏灰了菜单还能点"的漂移。
    ///
    /// 两类项会被跳过：
    ///   - property("commandUnavailable") 为真（命令未注册，构建期已永久禁用）
    ///   - 规则为 Always（恒可用，无需干预，也避免覆盖 setAllMenusEnabled 之类的全局置灰）
    void refreshCommandStates(const CommandUiSnapshot& snapshot);



    /// 所属主窗口（供命令分发器转发工作台切换等窗口级动作）
    WorkbenchWindow* workbenchWindow() const
    {
        return m_window;
    }
    /// 通过菜单配置重建菜单（JSON 驱动）
    void rebuildMenusFromConfig();
    /// 根据工作台和命令可用性过滤配置菜单，便于测试与复用
    static std::vector<MenuDef> filterMenusForWorkbench(const std::vector<MenuDef>& menus,
        const QString& workbenchId,
        const std::function<bool(const QString&)>& commandAvailable,
        const QString& workbenchKind = QString());

    /// 让帮助/主题等固定菜单保持在可预期的工作台中，避免配置误入
    static bool isMenuGroupAllowedForWorkbench(const QString& commandId, const QString& workbenchKind);
    /// 清理全局快捷键动作（Undo/Redo），切换工作台时调用
    void clearGlobalShortcuts();
    void createBaseMenus();
    void initializeMenuSkeleton();
    void initializeThemeMenuSkeleton();

    void refreshFileMenuForWorkbench(const QString& workbenchId);
    void refreshDrawMenuForWorkbench(const QString& workbenchId);
    /// 从命令目录汇总导入支持格式的菜单项，供 File → Import 统一展示
    void refreshImportMenuForWorkbench(const QString& workbenchId);
    void refreshExportMenuForWorkbench(const QString& workbenchId);
    void refreshEditMenuForWorkbench(const QString& workbenchId);
    void refreshModifyMenuForWorkbench(const QString& workbenchId);
    void refreshAlgorithmMenuForWorkbench(const QString& workbenchId);
    void refreshWorkbenchMenuChecks(const QString& workbenchId);
    void refreshThemeMenuChecks(const QString& themeId);

    void syncGridSnapMenuState();
    void refreshGridSnapMenuChecks();

    /// 根据当前激活的绘图工具同步 Draw 菜单勾选态（与左侧工具栏联动）
    void syncDrawMenuToTool(const QString& toolName);

    QMenu* fileMenu() const
    {
        return m_menuState.fileMenu;
    }

    QMenu* recentFilesMenu() const
    {
        return m_menuState.recentFilesMenu;
    }

    QMenu* importMenu() const
    {
        return m_menuState.importMenu;
    }

    QMenu* exportMenu() const
    {
        return m_menuState.exportMenu;
    }

    QAction* workbench2DAction() const
    {
        return m_menuState.workbench2DAction;
    }

    QAction* workbench3DAction() const
    {
        return m_menuState.workbench3DAction;
    }

private:
    void buildFileMenu();
    void buildViewMenu();
    void buildHelpMenu();
    void buildLegacyMenus();
    /// 菜单项统一分发：优先走当前工作台的 dispatchCommand（与 config 模式/工具栏/右键一致），
    /// 无工作台时回退到 OperationBus::run
    void dispatchCommandSafely(const QString& commandId);

    // 统一菜单项工厂：集中最终创建 QAction、设置 command、图标解析、日志与命令分发。
    // 取代原先 Draw/Edit/Modify/Algo/Theme 各自独立的 addXxxAction lambda。
    enum MenuActionOption : int
    {
        MenuActionOption_None = 0,
        MenuActionOption_Checkable = 1 << 0,  // 可勾选（视口开关、单位、主题等）
        MenuActionOption_Theme = 1 << 1,      // 主题切换：commandId 即主题 ID，直接触发 triggerTheme
    };

    QAction* addMenuAction(QMenu* menu,
        const QString& text,
        const QString& commandId,
        const QString& fallbackIcon = QString(),
        int options = MenuActionOption_None);

    /// 绑定配置菜单状态到状态中心（checkable 状态同步）
    void bindConfiguredMenuState();
    /// 刷新配置菜单状态
    void refreshConfiguredMenuState();

    /// 遍历菜单栏（config-driven）与 legacy 菜单指针下所有带 commandId 的叶子动作。
    /// checked 同步与 enabled 同步共用这一份遍历，避免两处各写一遍递归导致覆盖范围漂移。
    void forEachCommandAction(const std::function<void(QAction*, const QString&)>& visitor) const;


    struct MenuState
    {
        QMenu* fileMenu{ nullptr };
        QMenu* editMenu{ nullptr };
        QMenu* drawMenu{ nullptr };
        QMenu* modifyMenu{ nullptr };
        QMenu* viewMenu{ nullptr };
        QMenu* algorithmMenu{ nullptr };
        QMenu* helpMenu{ nullptr };
        QMenu* toolsMenu{ nullptr };
        QMenu* importMenu{ nullptr };
        QMenu* exportMenu{ nullptr };
        QMenu* recentFilesMenu{ nullptr };
        QMenu* rotateMenu{ nullptr };
        QMenu* mirrorMenu{ nullptr };
        QMenu* alignMenu{ nullptr };
        QMenu* pathOpsMenu{ nullptr };
        QMenu* unitMenu{ nullptr };
        QMenu* gridSnapMenu{ nullptr };
        QMenu* zoomMenu{ nullptr };
        QMenu* languageMenu{ nullptr };
        QMenu* helpThemeMenu{ nullptr };
        QAction* workbench2DAction{ nullptr };
        QAction* workbench3DAction{ nullptr };
        QMenu* themeMenu{ nullptr };
    } m_menuState;

    WorkbenchWindow* m_window;
    QMetaObject::Connection m_languageChangedConn;
    QMetaObject::Connection m_themeChangedConn;
    OperationBus* m_operationBus{ nullptr };
    UiStateCenter* m_stateCenter{ nullptr };
    const UiFrameworkServices* m_frameworkServices{ nullptr };
    const UiServices* m_uiServices{ nullptr };
    UiWorkbench* m_workbench{ nullptr };
    WorkbenchFactory m_workbenchFactory;
    std::function<void(const QString&)> m_viewportZoomHandler;
    std::function<void()> m_testViewHandler;
    // Draw 菜单绘图工具动作组（互斥单选，与左侧工具栏选中态联动）
    QActionGroup* m_drawToolActionGroup{ nullptr };
    // 全局编辑快捷键动作（窗口级，需在切换工作台时显式清理）
    std::vector<QAction*> m_editShortcuts;
    // 客户 UI 配置不再由本类持有：统一取自 UiConfigurationManager::shared()，
    // 保证菜单 / 工具栏 / Dock / 状态栏 / 右键菜单消费同一份配置（P0-1）
    std::unique_ptr<UiLayoutBuilder> m_menuLayoutBuilder;
    std::unique_ptr<UiPanelRegistry> m_menuPanelRegistry;
    // 配置菜单命令分发器：生命周期随本对象，菜单项触发回调期间必须长期有效
    std::unique_ptr<MenuDispatcher> m_dispatcher;
    // 防止每次 rebuildAllMenus 重复连接状态中心信号
    bool m_configStateBound{ false };
};
