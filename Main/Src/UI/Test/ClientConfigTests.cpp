/**
 * @file ClientConfigTests.cpp
 * @brief 客户化 UI 配置（第三阶段）：加载器 / 面板注册表 / 布局构建器单元测试
 *
 * 覆盖：
 * - UiConfigLoader：解析、extends 继承合并、错误处理
 * - UiPanelRegistry：注册 / 创建 / 未注册降级
 * - UiLayoutBuilder：Dock 构建、命令绑定（未注册命令禁用）
 * - UiConfigurationManager：配置应用 / 失败策略 / 重置
 */

#include <gtest/gtest.h>

#include "UI/ClientConfig/UiClientConfigBase.h"
#include "UI/ClientConfig/UiConfigLoader.h"
#include "UI/ClientConfig/UiConfigurationManager.h"
#include "UI/ClientConfig/UiLayoutBuilder.h"
#include "UI/ClientConfig/UiPanelRegistry.h"
#include "UI/Services/UiStateCenter.h"
#include "UI/Workbench/WorkbenchMenuManager.h"

#include <QMainWindow>
#include <QDockWidget>
#include <QFile>
#include <QMenuBar>
#include <QSet>
#include <QTemporaryDir>
#include <QToolBar>
#include <QWidget>

#include <QApplication>

#include <memory>

namespace
{
    // MainTests 为 gtest 控制台程序（GTest::gtest_main），默认无 QApplication。
    // 通过 gtest 全局环境在测试开始前构造一次 QApplication，使需要 QWidget 的测试可运行。
    class TestAppEnvironment : public ::testing::Environment
    {
    public:
        void SetUp() override
        {
            if (!qApp)
            {
                static int argc = 1;
                static char appName[] = "MainTests";
                static char* argv[] = { appName, nullptr };
                g_app = std::make_unique<QApplication>(argc, argv);
            }
        }

    private:
        std::unique_ptr<QApplication> g_app;
    };

    // 注册全局测试环境（在第一个测试运行前执行 SetUp）
    ::testing::Environment* const g_testEnv = ::testing::AddGlobalTestEnvironment((new TestAppEnvironment));
    QString writeTempConfig(const QString& fileName, const QString& json)
    {
        static QTemporaryDir tempDir;
        const QString path = tempDir.filePath(fileName);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text))
        {
            file.write(json.toUtf8());
            file.close();
        }
        return path;
    }

    // 测试用命令分发器：记录分发到的命令，支持注册表查询
    struct FakeDispatcher : public IUiCommandDispatcher
    {
        QStringList dispatched;
        QSet<QString> registered;

        bool isCommandRegistered(const QString& commandId) const override
        {
            return registered.contains(commandId);
        }
        void dispatch(const QString& commandId) override
        {
            dispatched.push_back(commandId);
        }
    };
} // namespace

// ==================== UiConfigLoader ====================

TEST(ClientConfigLoaderTest, ParsesMinimalConfig)
{
    const QString path = writeTempConfig(QStringLiteral("minimal.json"), QStringLiteral(R"({
            "meta": { "clientId": "test", "clientName": "Test", "version": "1.0" },
            "menus": [ { "id": "file", "label": "File", "items": [
                { "type": "action", "id": "file.open", "label": "Open", "command": "file.open" }
            ]}],
            "docks": [ { "id": "DockA", "title": "A", "position": "left", "widgetType": "PanelX", "visible": true } ],
            "shortcuts": [ { "command": "file.open", "keys": "Ctrl+O" } ]
        })"));

    UiConfigLoader loader(path);
    auto config = loader.load();
    ASSERT_TRUE(config.has_value()) << loader.lastError().toStdString();

    EXPECT_EQ(config->meta.clientId, QStringLiteral("test"));
    ASSERT_EQ(config->menus.size(), 1u);
    EXPECT_EQ(config->menus[0].id, QStringLiteral("file"));
    ASSERT_EQ(config->menus[0].items.size(), 1u);
    ASSERT_EQ(config->docks.size(), 1u);
    EXPECT_EQ(config->docks[0].position, DockPosition::Left);
    ASSERT_EQ(config->shortcuts.size(), 1u);
}

TEST(ClientConfigLoaderTest, LoadsFromQtResource)
{
    // configs.qrc 嵌入所有客户配置，经 AUTORCC 编译进测试二进制
    UiConfigLoader loader(QStringLiteral(":/configs/san_yi.json"));
    auto config = loader.load();
    ASSERT_TRUE(config.has_value()) << loader.lastError().toStdString();
    EXPECT_EQ(config->meta.clientId, QStringLiteral("san_yi"));
    EXPECT_FALSE(config->menus.empty());
    EXPECT_FALSE(config->docks.empty());
}

TEST(ClientConfigLoaderTest, SupportsCheckableEntries)
{
    UiConfigLoader loader(QStringLiteral(":/configs/base.json"));
    auto config = loader.load();
    ASSERT_TRUE(config.has_value()) << loader.lastError().toStdString();

    bool foundGrid = false;
    bool foundWireframe = false;
    for (const auto& menu : config->menus)
    {
        for (const auto& item : menu.items)
        {
            if (std::holds_alternative<SubMenuDef>(item))
            {
                const auto& sub = std::get<SubMenuDef>(item);
                for (const auto& subItem : sub.items)
                {
                    if (!std::holds_alternative<MenuActionDef>(subItem))
                        continue;
                    const auto& action = std::get<MenuActionDef>(subItem);
                    if (action.commandId == QStringLiteral("view.grid"))
                    {
                        foundGrid = true;
                        EXPECT_TRUE(action.checkable);
                    }
                    if (action.commandId == QStringLiteral("view.wireframe"))
                    {
                        foundWireframe = true;
                        EXPECT_TRUE(action.checkable);
                    }
                }
            }
        }
    }
    EXPECT_TRUE(foundGrid);
    EXPECT_TRUE(foundWireframe);
}

TEST(ClientConfigLoaderTest, MissingFileReportsError)
{
    UiConfigLoader loader(QStringLiteral(":/configs/does_not_exist.json"));
    auto config = loader.load();
    EXPECT_FALSE(config.has_value());
    EXPECT_FALSE(loader.lastError().isEmpty());
}

TEST(ClientConfigLoaderTest, ExtendsMergesParentConfig)
{
    // 子配置 extends 父配置：同 id 覆盖、新 id 追加、未定义项继承
    const QString basePath = writeTempConfig(QStringLiteral("base_merge.json"), QStringLiteral(R"({
            "meta": { "clientId": "base" },
            "menus": [
                { "id": "file", "label": "File", "items": [
                    { "type": "action", "id": "file.open", "label": "Open", "command": "file.open" }
                ]},
                { "id": "edit", "label": "Edit", "items": [] }
            ],
            "docks": [ { "id": "DockBase", "title": "Base", "position": "right", "widgetType": "P", "visible": true } ]
        })"));
    const QString childPath =
        writeTempConfig(QStringLiteral("child_merge.json"), QStringLiteral(R"({
            "meta": { "clientId": "child" },
            "extends": "<BASE>",
            "menus": [
                { "id": "file", "label": "File", "items": [
                    { "type": "action", "id": "file.new", "label": "New", "command": "file.new" }
                ]}
            ]
        })")
            .replace(QStringLiteral("<BASE>"), basePath));

    UiConfigLoader loader(childPath);
    auto config = loader.load();
    ASSERT_TRUE(config.has_value()) << loader.lastError().toStdString();

    // 覆盖：file 菜单采用子配置（含新增 action）
    // 追加：edit 菜单继承自父配置
    bool foundFile = false;
    bool foundEdit = false;
    for (const auto& menu : config->menus)
    {
        if (menu.id == QStringLiteral("file"))
        {
            foundFile = true;
            EXPECT_EQ(menu.items.size(), 1u);
        }
        if (menu.id == QStringLiteral("edit"))
            foundEdit = true;
    }
    EXPECT_TRUE(foundFile);
    EXPECT_TRUE(foundEdit);

    // 继承：docks 未在子配置中定义 → 全量继承父配置
    ASSERT_EQ(config->docks.size(), 1u);
    EXPECT_EQ(config->docks[0].id, QStringLiteral("DockBase"));
}

// ==================== UiPanelRegistry ====================

TEST(ClientConfigPanelRegistryTest, RegisterCreateQuery)
{
    UiPanelRegistry registry;

    EXPECT_FALSE(registry.isPanelRegistered(QStringLiteral("PanelA")));

    registry.registerPanel(QStringLiteral("PanelA"), [](QWidget* parent) { return new QWidget(parent); });

    EXPECT_TRUE(registry.isPanelRegistered(QStringLiteral("PanelA")));
    EXPECT_TRUE(registry.registeredPanelIds().contains(QStringLiteral("PanelA")));

    QWidget host;
    QWidget* created = registry.createPanel(QStringLiteral("PanelA"), &host);
    ASSERT_NE(created, nullptr);
    EXPECT_EQ(created->parent(), &host);
    delete created;
}

TEST(ClientConfigPanelRegistryTest, UnregisteredReturnsNull)
{
    UiPanelRegistry registry;
    QWidget host;
    EXPECT_EQ(registry.createPanel(QStringLiteral("NoSuchPanel"), &host), nullptr);
    EXPECT_FALSE(registry.isPanelRegistered(QStringLiteral("NoSuchPanel")));
}

TEST(ClientConfigPanelRegistryTest, OverrideReplacesFactory)
{
    UiPanelRegistry registry;
    registry.registerPanel(QStringLiteral("P"), [](QWidget* parent) -> QWidget* { return new QWidget(parent); });
    registry.registerPanel(QStringLiteral("P"), [](QWidget* parent) -> QWidget* { return new QMainWindow(parent); });

    QWidget host;
    QWidget* created = registry.createPanel(QStringLiteral("P"), &host);
    ASSERT_NE(created, nullptr);
    EXPECT_NE(qobject_cast<QMainWindow*>(created), nullptr);
    delete created;
}

// ==================== UiLayoutBuilder ====================

TEST(ClientConfigLayoutBuilderTest, BuildsDocksFromConfig)
{
    UiPanelRegistry registry;
    registry.registerPanel(QStringLiteral("PanelX"), [](QWidget* parent) { return new QWidget(parent); });

    FakeDispatcher dispatcher;
    QMainWindow window;
    UiLayoutBuilder builder(&window, &dispatcher, &registry);

    DockDef leftDock;
    leftDock.id = QStringLiteral("left");
    leftDock.title = QStringLiteral("Left");
    leftDock.position = DockPosition::Left;
    leftDock.widgetType = QStringLiteral("PanelX");
    leftDock.visible = true;

    builder.buildDocks({ leftDock });

    ASSERT_EQ(builder.builtDocks().size(), 1u);
    auto* dock = builder.builtDocks().front();
    ASSERT_NE(dock, nullptr);
    EXPECT_EQ(dock->objectName(), QStringLiteral("left"));
    EXPECT_EQ(window.findChildren<QDockWidget*>().size(), 1);
}

TEST(ClientConfigLayoutBuilderTest, UnknownPanelFallsBackToPlaceholder)
{
    UiPanelRegistry emptyRegistry;
    FakeDispatcher dispatcher;
    QMainWindow window;
    UiLayoutBuilder builder(&window, &dispatcher, &emptyRegistry);

    DockDef dock;
    dock.id = QStringLiteral("d1");
    dock.title = QStringLiteral("D1");
    dock.position = DockPosition::Right;
    dock.widgetType = QStringLiteral("UnregisteredPanel");

    builder.buildDocks({ dock });
    ASSERT_EQ(builder.builtDocks().size(), 1u);
    EXPECT_EQ(window.findChildren<QDockWidget*>().size(), 1);
}

TEST(ClientConfigLayoutBuilderTest, BindsRegisteredCommandToAction)
{
    FakeDispatcher dispatcher;
    dispatcher.registered.insert(QStringLiteral("cmd.known"));

    QMainWindow window;
    UiLayoutBuilder builder(&window, &dispatcher, nullptr);

    QAction* action = new QAction(&window);
    builder.bindAction(action, QStringLiteral("cmd.known"));
    EXPECT_TRUE(action->isEnabled());

    action->trigger();
    ASSERT_EQ(dispatcher.dispatched.size(), 1u);
    EXPECT_EQ(dispatcher.dispatched[0], QStringLiteral("cmd.known"));
    delete action;
}

TEST(ClientConfigLayoutBuilderTest, UnknownCommandDisablesAction)
{
    FakeDispatcher dispatcher;
    QMainWindow window;
    UiLayoutBuilder builder(&window, &dispatcher, nullptr);

    QAction* action = new QAction(&window);
    builder.bindAction(action, QStringLiteral("cmd.missing"));
    EXPECT_FALSE(action->isEnabled());
    EXPECT_FALSE(action->toolTip().isEmpty());
    delete action;
}

TEST(ClientConfigLayoutBuilderTest, CheckableActionRespectsInitialState)
{
    FakeDispatcher dispatcher;
    dispatcher.registered.insert(QStringLiteral("view.grid"));
    QMainWindow window;
    UiLayoutBuilder builder(&window, &dispatcher, nullptr);

    MenuActionDef actionDef;
    actionDef.id = QStringLiteral("view.grid");
    actionDef.label = QStringLiteral("Grid");
    actionDef.commandId = QStringLiteral("view.grid");
    actionDef.checkable = true;
    actionDef.checked = true;

    QMenu menu(&window);
    auto* action = menu.addAction(actionDef.label);
    action->setCheckable(actionDef.checkable);
    action->setChecked(actionDef.checked);
    builder.bindAction(action, actionDef.commandId, actionDef.label, actionDef.iconName, QStringLiteral("3D"));

    EXPECT_TRUE(action->isCheckable());
    EXPECT_TRUE(action->isChecked());
}

// ==================== UiConfigurationManager ====================

TEST(ClientConfigManagerTest, ApplyConfigurationLoadsData)
{
    const QString path = writeTempConfig(QStringLiteral("mgr_test.json"), QStringLiteral(R"({
            "meta": { "clientId": "mgr" },
            "toolbars": [ { "id": "tb1", "title": "TB1", "position": "top", "workbench": "global", "items": [] } ]
        })"));

    UiConfigurationManager mgr;
    EXPECT_TRUE(mgr.applyConfiguration(path, ConfigFallbackPolicy::Strict));
    ASSERT_NE(mgr.configData(), nullptr);
    ASSERT_EQ(mgr.configData()->toolBars.size(), 1u);
}

TEST(ClientConfigManagerTest, StrictPolicyFailsOnMissingConfig)
{
    UiConfigurationManager mgr;
    // Strict 策略：加载失败直接返回 false，不回退
    EXPECT_FALSE(mgr.applyConfiguration(QStringLiteral(":/configs/nope.json"), ConfigFallbackPolicy::Strict));
    EXPECT_EQ(mgr.configData(), nullptr);
}

TEST(ClientConfigManagerTest, ResetClearsState)
{
    const QString path =
        writeTempConfig(QStringLiteral("mgr_reset.json"), QStringLiteral(R"({ "meta": { "clientId": "mgr" } })"));

    UiConfigurationManager mgr;
    ASSERT_TRUE(mgr.applyConfiguration(path));
    ASSERT_NE(mgr.configData(), nullptr);

    mgr.reset();
    EXPECT_EQ(mgr.configData(), nullptr);
}

TEST(ClientConfigStateCenterTest, MetadataRoundTrip)
{
    UiStateCenter center;
    QVariantMap meta;
    meta.insert(QStringLiteral("gridVisible"), true);
    meta.insert(QStringLiteral("snapEnabled"), false);
    meta.insert(QStringLiteral("orthoMode"), true);
    meta.insert(QStringLiteral("angleSnap"), false);
    center.setMetadata(meta);

    const auto snapshot = center.snapshot();
    EXPECT_TRUE(snapshot.metadata.value(QStringLiteral("gridVisible")).toBool());
    EXPECT_FALSE(snapshot.metadata.value(QStringLiteral("snapEnabled")).toBool());
    EXPECT_TRUE(snapshot.metadata.value(QStringLiteral("orthoMode")).toBool());
    EXPECT_FALSE(snapshot.metadata.value(QStringLiteral("angleSnap")).toBool());
}

TEST(ClientConfigStateCenterTest, ThreeDVisibilityStateSync)
{
    UiStateCenter center;
    center.setCurrentWorkbenchId(QStringLiteral("3D"));
    QVariantMap meta;
    meta.insert(QStringLiteral("wireframe"), true);
    meta.insert(QStringLiteral("gridVisible"), true);
    meta.insert(QStringLiteral("bbox"), false);
    meta.insert(QStringLiteral("floor"), true);
    center.setMetadata(meta);

    const auto snapshot = center.snapshot();
    EXPECT_EQ(snapshot.currentWorkbenchId, QStringLiteral("3D"));
    EXPECT_TRUE(snapshot.metadata.value(QStringLiteral("wireframe")).toBool());
    EXPECT_TRUE(snapshot.metadata.value(QStringLiteral("gridVisible")).toBool());
    EXPECT_FALSE(snapshot.metadata.value(QStringLiteral("bbox")).toBool());
    EXPECT_TRUE(snapshot.metadata.value(QStringLiteral("floor")).toBool());
}

TEST(ClientConfigLoaderTest, WorkbenchFilterKeepsOnlyMatchingEntries)
{
    const QString path = writeTempConfig(QStringLiteral("workbench_filter.json"), QStringLiteral(R"({
            "meta": { "clientId": "wb" },
            "menus": [
                { "id": "view", "label": "View", "workbenches": ["3D"], "items": [
                    { "type": "action", "id": "view.front", "label": "Front", "command": "view.front", "workbenches": ["3D"] },
                    { "type": "action", "id": "view.grid", "label": "Grid", "command": "view.grid", "workbenches": ["2D"] }
                ]}
            ]
        })"));

    UiConfigLoader loader(path);
    auto config = loader.load();
    ASSERT_TRUE(config.has_value()) << loader.lastError().toStdString();
    ASSERT_EQ(config->menus.size(), 1u);
    EXPECT_EQ(config->menus[0].id, QStringLiteral("view"));
    ASSERT_EQ(config->menus[0].items.size(), 2u);
}

TEST(ClientConfigLoaderTest, ThreeDMenuFilterDropsUnknownGroups)
{
    auto commandAvailable = [](const QString& commandId) {
        return commandId == QStringLiteral("view.front") || commandId == QStringLiteral("model.make_box");
        };

    MenuDef viewMenu;
    viewMenu.id = QStringLiteral("view");
    viewMenu.label = QStringLiteral("View");
    viewMenu.workbenches = { QStringLiteral("3D") };

    MenuActionDef goodView;
    goodView.id = QStringLiteral("view.front");
    goodView.label = QStringLiteral("Front");
    goodView.commandId = QStringLiteral("view.front");
    goodView.workbenches = { QStringLiteral("3D") };
    viewMenu.items.push_back(goodView);

    MenuActionDef badView;
    badView.id = QStringLiteral("misc.hidden");
    badView.label = QStringLiteral("Hidden");
    badView.commandId = QStringLiteral("misc.hidden");
    badView.workbenches = { QStringLiteral("3D") };
    viewMenu.items.push_back(badView);

    MenuDef toolsMenu;
    toolsMenu.id = QStringLiteral("tools");
    toolsMenu.label = QStringLiteral("Tools");
    toolsMenu.workbenches = { QStringLiteral("3D") };
    MenuActionDef badTools;
    badTools.id = QStringLiteral("tools.misc");
    badTools.label = QStringLiteral("Misc");
    badTools.commandId = QStringLiteral("tools.misc");
    badTools.workbenches = { QStringLiteral("3D") };
    toolsMenu.items.push_back(badTools);

    const auto filtered = WorkbenchMenuManager::filterMenusForWorkbench({ viewMenu, toolsMenu }, QStringLiteral("3D"),
        commandAvailable, QStringLiteral("3D"));

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].id, QStringLiteral("view"));
    ASSERT_EQ(filtered[0].items.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<MenuActionDef>(filtered[0].items[0]));
    EXPECT_EQ(std::get<MenuActionDef>(filtered[0].items[0]).commandId, QStringLiteral("view.front"));
}

TEST(ClientConfigLoaderTest, HelpMenuKeepsOnlyApprovedSharedEntries)
{
    auto commandAvailable = [](const QString& commandId) {
        return commandId == QStringLiteral("help.about") || commandId == QStringLiteral("help.settings") ||
            commandId == QStringLiteral("help.shortcuts") || commandId == QStringLiteral("help.docs") ||
            commandId == QStringLiteral("theme.dark");
        };

    MenuDef helpMenu;
    helpMenu.id = QStringLiteral("help");
    helpMenu.label = QStringLiteral("Help");
    helpMenu.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    helpMenu.visibilityScope = QStringLiteral("shared");

    MenuActionDef docs;
    docs.id = QStringLiteral("help.docs");
    docs.label = QStringLiteral("Docs");
    docs.commandId = QStringLiteral("help.docs");
    docs.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    docs.visibilityScope = QStringLiteral("shared");
    helpMenu.items.push_back(docs);

    MenuActionDef hidden;
    hidden.id = QStringLiteral("help.legacy");
    hidden.label = QStringLiteral("Legacy");
    hidden.commandId = QStringLiteral("help.legacy");
    hidden.workbenches = { QStringLiteral("3D") };
    hidden.visibilityScope = QStringLiteral("shared");
    helpMenu.items.push_back(hidden);

    MenuDef themeMenu;
    themeMenu.id = QStringLiteral("theme");
    themeMenu.label = QStringLiteral("Theme");
    themeMenu.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    themeMenu.visibilityScope = QStringLiteral("shared");

    MenuActionDef dark;
    dark.id = QStringLiteral("theme.dark");
    dark.label = QStringLiteral("Dark");
    dark.commandId = QStringLiteral("theme.dark");
    dark.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    dark.visibilityScope = QStringLiteral("shared");
    themeMenu.items.push_back(dark);

    const auto filtered3D = WorkbenchMenuManager::filterMenusForWorkbench({ helpMenu, themeMenu }, QStringLiteral("3D"),
        commandAvailable, QStringLiteral("3D"));

    ASSERT_EQ(filtered3D.size(), 2u);
    EXPECT_EQ(filtered3D[0].visibilityScope, QStringLiteral("shared"));
    EXPECT_EQ(filtered3D[1].visibilityScope, QStringLiteral("shared"));
    ASSERT_EQ(filtered3D[0].items.size(), 1u);
    EXPECT_EQ(std::get<MenuActionDef>(filtered3D[0].items[0]).commandId, QStringLiteral("help.docs"));
    ASSERT_EQ(filtered3D[1].items.size(), 1u);
    EXPECT_EQ(std::get<MenuActionDef>(filtered3D[1].items[0]).commandId, QStringLiteral("theme.dark"));

    const auto filtered2D = WorkbenchMenuManager::filterMenusForWorkbench({ helpMenu, themeMenu }, QStringLiteral("2D"),
        commandAvailable, QStringLiteral("2D"));
    ASSERT_EQ(filtered2D.size(), 2u);
    EXPECT_EQ(filtered2D[0].visibilityScope, QStringLiteral("shared"));
    EXPECT_EQ(filtered2D[1].visibilityScope, QStringLiteral("shared"));
}

TEST(ClientConfigLoaderTest, WorkbenchSwitchDoesNotLeakMenuEntries)
{
    auto commandAvailable = [](const QString& commandId) {
        return commandId == QStringLiteral("file.open") || commandId == QStringLiteral("view.grid") ||
            commandId == QStringLiteral("view.wireframe") || commandId == QStringLiteral("model.make_box") ||
            commandId == QStringLiteral("help.about");
        };

    MenuDef fileMenu;
    fileMenu.id = QStringLiteral("file");
    fileMenu.label = QStringLiteral("File");
    fileMenu.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    fileMenu.visibilityScope = QStringLiteral("shared");

    MenuActionDef openAction;
    openAction.id = QStringLiteral("file.open");
    openAction.label = QStringLiteral("Open");
    openAction.commandId = QStringLiteral("file.open");
    openAction.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    openAction.visibilityScope = QStringLiteral("shared");
    fileMenu.items.push_back(openAction);

    MenuDef viewMenu;
    viewMenu.id = QStringLiteral("view");
    viewMenu.label = QStringLiteral("View");
    viewMenu.workbenches = { QStringLiteral("3D") };
    viewMenu.visibilityScope = QStringLiteral("3D");

    MenuActionDef wireframe;
    wireframe.id = QStringLiteral("view.wireframe");
    wireframe.label = QStringLiteral("Wireframe");
    wireframe.commandId = QStringLiteral("view.wireframe");
    wireframe.workbenches = { QStringLiteral("3D") };
    wireframe.visibilityScope = QStringLiteral("3D");
    viewMenu.items.push_back(wireframe);

    MenuActionDef grid;
    grid.id = QStringLiteral("view.grid");
    grid.label = QStringLiteral("Grid");
    grid.commandId = QStringLiteral("view.grid");
    grid.workbenches = { QStringLiteral("2D") };
    grid.visibilityScope = QStringLiteral("2D");
    viewMenu.items.push_back(grid);

    MenuDef modelMenu;
    modelMenu.id = QStringLiteral("model");
    modelMenu.label = QStringLiteral("Model");
    modelMenu.workbenches = { QStringLiteral("3D") };
    modelMenu.visibilityScope = QStringLiteral("3D");

    MenuActionDef makeBox;
    makeBox.id = QStringLiteral("model.make_box");
    makeBox.label = QStringLiteral("Make Box");
    makeBox.commandId = QStringLiteral("model.make_box");
    makeBox.workbenches = { QStringLiteral("3D") };
    makeBox.visibilityScope = QStringLiteral("3D");
    modelMenu.items.push_back(makeBox);

    MenuDef helpMenu;
    helpMenu.id = QStringLiteral("help");
    helpMenu.label = QStringLiteral("Help");
    helpMenu.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    helpMenu.visibilityScope = QStringLiteral("shared");

    MenuActionDef about;
    about.id = QStringLiteral("help.about");
    about.label = QStringLiteral("About");
    about.commandId = QStringLiteral("help.about");
    about.workbenches = { QStringLiteral("2D"), QStringLiteral("3D") };
    about.visibilityScope = QStringLiteral("shared");
    helpMenu.items.push_back(about);

    const std::vector<MenuDef> configMenus{ fileMenu, viewMenu, modelMenu, helpMenu };

    const auto filtered2D = WorkbenchMenuManager::filterMenusForWorkbench(configMenus, QStringLiteral("2D"),
        commandAvailable, QStringLiteral("2D"));
    const auto filtered3D = WorkbenchMenuManager::filterMenusForWorkbench(configMenus, QStringLiteral("3D"),
        commandAvailable, QStringLiteral("3D"));
    const auto filtered2DAgain = WorkbenchMenuManager::filterMenusForWorkbench(configMenus, QStringLiteral("2D"),
        commandAvailable, QStringLiteral("2D"));

    ASSERT_EQ(filtered2D.size(), 2u);
    ASSERT_EQ(filtered3D.size(), 4u);
    ASSERT_EQ(filtered2DAgain.size(), 2u);

    auto hasMenu = [](const std::vector<MenuDef>& menus, const QString& id) {
        for (const auto& menu : menus)
        {
            if (menu.id == id)
                return true;
        }
        return false;
        };

    EXPECT_TRUE(hasMenu(filtered2D, QStringLiteral("file")));
    EXPECT_TRUE(hasMenu(filtered2D, QStringLiteral("help")));
    EXPECT_FALSE(hasMenu(filtered2D, QStringLiteral("view")));
    EXPECT_FALSE(hasMenu(filtered2D, QStringLiteral("model")));

    EXPECT_TRUE(hasMenu(filtered3D, QStringLiteral("file")));
    EXPECT_TRUE(hasMenu(filtered3D, QStringLiteral("help")));
    EXPECT_TRUE(hasMenu(filtered3D, QStringLiteral("view")));
    EXPECT_TRUE(hasMenu(filtered3D, QStringLiteral("model")));

    EXPECT_TRUE(hasMenu(filtered2DAgain, QStringLiteral("file")));
    EXPECT_TRUE(hasMenu(filtered2DAgain, QStringLiteral("help")));
    EXPECT_FALSE(hasMenu(filtered2DAgain, QStringLiteral("view")));
    EXPECT_FALSE(hasMenu(filtered2DAgain, QStringLiteral("model")));

    ASSERT_EQ(filtered3D[1].items.size(), 1u);
    EXPECT_EQ(std::get<MenuActionDef>(filtered3D[1].items[0]).commandId, QStringLiteral("view.wireframe"));
    EXPECT_EQ(std::get<MenuActionDef>(filtered2D[0].items[0]).commandId, QStringLiteral("file.open"));
}