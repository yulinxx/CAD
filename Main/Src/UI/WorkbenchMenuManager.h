#pragma once

#include <functional>
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

using WorkbenchFactory = std::function<UiWorkbench* (const QString& workbenchId)>;

class WorkbenchMenuManager : public QObject
{
    Q_OBJECT

public:
    explicit WorkbenchMenuManager(WorkbenchWindow* window, QObject* parent = nullptr);

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
        QMenu* layerMenu{ nullptr };
        QMenu* unitMenu{ nullptr };
        QMenu* gridSnapMenu{ nullptr };
        QMenu* zoomMenu{ nullptr };
        QMenu* languageMenu{ nullptr };
        QMenu* helpThemeMenu{ nullptr };
        QAction* workbench2DAction{ nullptr };
        QAction* workbench3DAction{ nullptr };
        QMenu* themeMenu{ nullptr };
        QActionGroup* unitActionGroup{ nullptr };
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
};
