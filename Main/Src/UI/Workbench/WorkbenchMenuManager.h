#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <QObject>

class QAction;
class QActionGroup;
class QMenu;
class OperationBus;
class UiStateCenter;
class UiThemeService;
class UiWorkbench;
class WorkbenchWindow;
struct UiFrameworkServices;
struct UiServices;
struct MenuDef;
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
class IUiCommandDispatcher;
class UiConfigurationManager;
class UiPanelRegistry;
class UiLayoutBuilder;
struct UiConfigData;
struct MenuDispatcher;
#endif

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
    void setThemeService(UiThemeService* themeService);
    void setFrameworkServices(const UiFrameworkServices* services);
    void setUiServices(const UiServices* services);
    void setWorkbench(UiWorkbench* workbench);
    void setWorkbenchFactory(WorkbenchFactory factory);
    void setViewportZoomHandler(std::function<void(const QString&)> handler);

    void buildMenus();
    void buildThemeMenu();
    void bindMenuCommands();
    void bindShortcuts();
    void rebuildAllMenus();
    /// 算法执行 / 仅展示模式下，全局启用或禁用全部菜单交互（含子菜单项）。
    /// 例如算法后台运行时调用 setAllMenusEnabled(false) 使整栏置灰仅展示，完成后传 true 恢复。
    void setAllMenusEnabled(bool enabled);

    /// 所属主窗口（供命令分发器转发工作台切换等窗口级动作）
    WorkbenchWindow* workbenchWindow() const
    {
        return m_window;
    }
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    /// 通过菜单配置重建菜单（JSON 驱动）
    void rebuildMenusFromConfig();
#endif
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
    void refreshEditMenuForWorkbench(const QString& workbenchId);
    void refreshModifyMenuForWorkbench(const QString& workbenchId);
    void refreshAlgorithmMenuForWorkbench(const QString& workbenchId);
    void refreshWorkbenchMenuChecks(const QString& workbenchId);
    void refreshThemeMenuChecks(const QString& themeId);

    void syncGridSnapMenuState();
    void refreshGridSnapMenuChecks();

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
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    void bindConfiguredMenuState();
    void refreshConfiguredMenuState();
#endif

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
    UiThemeService* m_themeService{ nullptr };
    const UiFrameworkServices* m_frameworkServices{ nullptr };
    const UiServices* m_uiServices{ nullptr };
    UiWorkbench* m_workbench{ nullptr };
    WorkbenchFactory m_workbenchFactory;
    std::function<void(const QString&)> m_viewportZoomHandler;
    // 全局编辑快捷键动作（窗口级，需在切换工作台时显式清理）
    std::vector<QAction*> m_editShortcuts;
#ifdef SANYI_ENABLE_CONFIG_DRIVEN_UI
    std::unique_ptr<UiConfigurationManager> m_menuConfigManager;
    std::unique_ptr<UiLayoutBuilder> m_menuLayoutBuilder;
    std::unique_ptr<UiPanelRegistry> m_menuPanelRegistry;
    // 配置菜单命令分发器：生命周期随本对象，菜单项触发回调期间必须长期有效
    std::unique_ptr<MenuDispatcher> m_dispatcher;
    // 防止每次 rebuildAllMenus 重复连接状态中心信号
    bool m_configStateBound{ false };
#endif
};
