#include <QtTest/QtTest>

#include <optional>

#include "Model/Assembly/AssemblyPlanner.h"
#include "Model/Profile/ProfileParser.h"
#include "Model/Profile/ProfileLoadResult.h"
#include "UiShell/Contract/Command.h"
#include "UiShell/Contract/ITextProvider.h"

using namespace AA;

namespace
{
    std::optional<ProfileModel> parse(const QByteArray& json)
    {
        const ProfileLoadResult result = ProfileParser::loadFromJson(json);
        if (!result.isOk())
            return std::nullopt;
        return *result.profileModel;
    }

    const RegionInfo* findRegion(const AssemblyInfo& info, const QString& regionId)
    {
        for (const RegionInfo& region : info.regions)
        {
            if (region.strId == regionId)
                return &region;
        }
        return nullptr;
    }

    const PanelInfo* findPanel(const AssemblyInfo& info, const QString& panelId)
    {
        for (const RegionInfo& region : info.regions)
        {
            for (const PanelInfo& panel : region.panels)
            {
                if (panel.strId == panelId)
                    return &panel;
            }
        }
        return nullptr;
    }

    const char* const kPlannerProfile = R"json({
  "version": 2,
  "profileId": "planner",
  "regions": [
    { "id": "left", "type": "LeftDock", "order": 20,
      "areas": [
        { "id": "left.bottom", "order": 20 },
        { "id": "left.top", "order": 10 }
      ] },
    { "id": "center", "type": "CenterView", "order": 30 },
    { "id": "hidden", "type": "RightDock", "order": 40, "visible": false }
  ],
  "panels": [
    { "id": "late", "factory": "test.panel", "area": "left.bottom", "order": 20 },
    { "id": "early", "factory": "test.panel", "area": "left.top", "order": 10 },
    { "id": "vision", "factory": "test.panel", "area": "left.top", "order": 5,
      "requiredCapabilities": ["vision"] },
    { "id": "hidden.panel", "factory": "test.panel", "area": "hidden.main" }
  ]
})json";

    // 只认识被显式加入的命令，用于验证菜单里的未知命令会被过滤。
    class FakeCatalog : public ICommandCatalog
    {
    public:
        explicit FakeCatalog(const QStringList& ids)
        {
            for (const QString& id : ids)
            {
                CommandView view;
                view.strId = id;
                view.strTitleKey = id.toUpper();
                m_views.append(view);
            }
        }

        const CommandView* find(const QString& commandId) const override
        {
            for (const CommandView& view : m_views)
            {
                if (view.strId == commandId)
                    return &view;
            }
            return nullptr;
        }


    private:
        QList<CommandView> m_views;
    };

    const char* const kMenuProfile = R"json({
  "version": 2,
  "profileId": "menus",
  "regions": [ { "id": "center", "type": "CenterView" } ],
  "menus": [
    { "type": "submenu", "id": "help", "titleKey": "Help", "order": 30, "children": [ "help.about" ] },
    { "type": "submenu", "id": "file", "titleKey": "File", "order": 10,
      "children": [ "file.new", "file.unknown" ] }
  ]
})json";
} // namespace

class AssemblyPlannerTest : public QObject
{
    Q_OBJECT

private slots:
    void sortsRegionsAreasAndPanels();
    void flagGatesPanel();
    void samePanelMovesWithArea();
    void menusAreSortedAndUnknownCommandsFiltered();
    void menusKeepAllCommandsWithoutCatalog();
    void titleBarPlanPlatformDefault();
    void titleBarPlanFiltersButtonsAndClamps();
    void disabledTitleBarRemovesRegion();
    void titleBarRegionIgnoredOnNonWindows();
    void nativeMenuBarFollowsProfile();
    void structureKeyReflectsPlatformTitleBarMode();
};

// 区域、子区域、面板都按 order 升序重排，同时剔除隐藏区域与开关不足的面板。
void AssemblyPlannerTest::sortsRegionsAreasAndPanels()
{
    const auto model = parse(QByteArray(kPlannerProfile));
    QVERIFY(model.has_value());

    const AssemblyInfo info = planAssembly(*model, FeatureFlagSet());

    QCOMPARE(info.regions.size(), 2);
    QCOMPARE(info.regions.at(0).strId.toStdString(), std::string("left"));
    QCOMPARE(info.regions.at(1).strId.toStdString(), std::string("center"));

    QCOMPARE(info.regions.at(0).paneAreas.size(), 2);
    QCOMPARE(info.regions.at(0).paneAreas.at(0).strId.toStdString(), std::string("left.top"));
    QCOMPARE(info.regions.at(0).paneAreas.at(1).strId.toStdString(), std::string("left.bottom"));

    // 默认开关快照没有 vision，vision 面板被过滤；隐藏区域里的面板也不出现。
    QCOMPARE(info.regions.at(0).panels.size(), 2);
    QVERIFY(findPanel(info, QStringLiteral("early")) != nullptr);
    QCOMPARE(findPanel(info, QStringLiteral("early"))->paneAreaId.toStdString(), std::string("left.top"));
    QVERIFY(findPanel(info, QStringLiteral("vision")) == nullptr);
    QVERIFY(findPanel(info, QStringLiteral("hidden.panel")) == nullptr);
    QVERIFY(findRegion(info, QStringLiteral("hidden")) == nullptr);
}

void AssemblyPlannerTest::flagGatesPanel()
{
    const auto model = parse(QByteArray(kPlannerProfile));
    QVERIFY(model.has_value());

    const FeatureFlagSet flags(QSet<QString>{QStringLiteral("vision")});

    const AssemblyInfo info = planAssembly(*model, flags);

    QVERIFY(findPanel(info, QStringLiteral("vision")) != nullptr);

    // 补上 vision 开关后，左侧子区域里的面板由 2 个增加到 3 个。
    const RegionInfo* left = findRegion(info, QStringLiteral("left"));
    QVERIFY(left != nullptr);
    QCOMPARE(left->panels.size(), 3);
}

// 同一份面板定义只改 area，就会在两个区域之间移动，说明归属完全由 profile 决定。
void AssemblyPlannerTest::samePanelMovesWithArea()
{
    const QByteArray leftJson = R"json({
      "version": 2,
      "profileId": "left",
      "regions": [
        { "id": "left", "type": "LeftDock" },
        { "id": "right", "type": "RightDock" }
      ],
      "panels": [ { "id": "tools", "factory": "test.panel", "area": "left.main" } ]
    })json";

    const QByteArray rightJson = R"json({
      "version": 2,
      "profileId": "right",
      "regions": [
        { "id": "left", "type": "LeftDock" },
        { "id": "right", "type": "RightDock" }
      ],
      "panels": [ { "id": "tools", "factory": "test.panel", "area": "right.main" } ]
    })json";

    const auto leftModel = parse(leftJson);
    const auto rightModel = parse(rightJson);
    QVERIFY(leftModel.has_value());
    QVERIFY(rightModel.has_value());


    const AssemblyInfo leftInfo = planAssembly(*leftModel, FeatureFlagSet());
    QVERIFY(findRegion(leftInfo, QStringLiteral("left")) != nullptr);
    QCOMPARE(findRegion(leftInfo, QStringLiteral("left"))->panels.size(), 1);
    QCOMPARE(findRegion(leftInfo, QStringLiteral("left"))->panels.at(0).strId.toStdString(),
        std::string("tools"));

    const AssemblyInfo rightInfo = planAssembly(*rightModel, FeatureFlagSet());
    QVERIFY(findRegion(rightInfo, QStringLiteral("right")) != nullptr);
    QCOMPARE(findRegion(rightInfo, QStringLiteral("right"))->panels.size(), 1);
    QCOMPARE(findRegion(rightInfo, QStringLiteral("right"))->panels.at(0).strId.toStdString(),
        std::string("tools"));
}

void AssemblyPlannerTest::menusAreSortedAndUnknownCommandsFiltered()
{
    const auto model = parse(QByteArray(kMenuProfile));
    QVERIFY(model.has_value());

    const FakeCatalog catalog(QStringList{ QStringLiteral("file.new"),
                                          QStringLiteral("help.about") });

    const AssemblyInfo info = planAssembly(*model, FeatureFlagSet(), &catalog);

    QCOMPARE(info.menus.size(), 2);
    QCOMPARE(info.menus.at(0).strId.toStdString(), std::string("file"));
    QCOMPARE(info.menus.at(1).strId.toStdString(), std::string("help"));

    // file.unknown 不在 catalog 中，被丢弃并产生 warning。
    // children 现在是递归结构：每个子项是 MenuNodeInfo（Command 节点）
    int fileCommandCount = 0;
    QStringList fileCommandIds;
    for (const MenuNodeInfo& child : info.menus.at(0).children)
    {
        if (child.type == MenuNodeType::Command)
        {
            ++fileCommandCount;
            fileCommandIds.append(child.strCmdId);
        }
    }
    QCOMPARE(fileCommandCount, 1);
    QCOMPARE(fileCommandIds.at(0).toStdString(), std::string("file.new"));

    bool warned = false;
    for (const QString& warning : info.strListWarnings)
    {
        if (warning.contains(QStringLiteral("file.unknown")))
            warned = true;
    }
    QVERIFY(warned);
}

// 没有 catalog 时不做可用性过滤，纯配置场景也要保留全部命令。
void AssemblyPlannerTest::menusKeepAllCommandsWithoutCatalog()
{
    const auto model = parse(QByteArray(kMenuProfile));
    QVERIFY(model.has_value());

    const AssemblyInfo info = planAssembly(*model, FeatureFlagSet());

    QCOMPARE(info.menus.size(), 2);
    int fileCommandCount = 0;
    QStringList fileCommandIds;
    for (const MenuNodeInfo& child : info.menus.at(0).children)
    {
        if (child.type == MenuNodeType::Command)
        {
            ++fileCommandCount;
            fileCommandIds.append(child.strCmdId);
        }
    }
    QCOMPARE(fileCommandCount, 2);
    QCOMPARE(fileCommandIds.at(1).toStdString(), std::string("file.unknown"));
}

// 平台固定：Windows 自绘，macOS/Linux 原生。不从 profile 读 nativeTitleBar。
void AssemblyPlannerTest::titleBarPlanPlatformDefault()
{
    const QByteArray json = R"json({
      "version": 2,
      "profileId": "tb",
      "regions": [
        { "id": "titlebar", "type": "TitleBar" },
        { "id": "center", "type": "CenterView" }
      ]
    })json";

    const auto model = parse(json);
    QVERIFY(model.has_value());

    const AssemblyInfo info = planAssembly(*model, FeatureFlagSet());

#if defined(Q_OS_WIN)
    // Windows 自绘标题栏 + TitleBar 区域 → 生成自绘控件。
    QVERIFY(!info.bNativeTitleBar);
    QVERIFY(info.titleBar.bPresent);
#else
    // macOS/Linux 原生标题栏 + TitleBar 区域 → 跳过自绘控件。
    QVERIFY(info.bNativeTitleBar);
    QVERIFY(!info.titleBar.bPresent);
#endif
    QCOMPARE(info.nMinWidth, 800);
    QCOMPARE(info.nMinHeight, 600);
}

void AssemblyPlannerTest::titleBarPlanFiltersButtonsAndClamps()
{
    const QByteArray json = R"json({
      "version": 2,
      "profileId": "tb",
      "titleBar": {
        "title": "工作台",
        "buttons": [ "close", "bogus", "close", "maximize" ],
        "height": 999
      },
      "regions": [ { "id": "titlebar", "type": "TitleBar" } ]
    })json";

    const auto model = parse(json);
    QVERIFY(model.has_value());

    const AssemblyInfo info = planAssembly(*model, FeatureFlagSet());

#if defined(Q_OS_WIN)
    // bogus 被丢弃并产生 warning，重复的 close 只保留首次出现的位置。
    QVERIFY(info.titleBar.bPresent);
    QCOMPARE(info.titleBar.strTitle.toStdString(), std::string("工作台"));
    QCOMPARE(info.titleBar.nHeight, 96);
    QCOMPARE(info.titleBar.buttons.size(), 2);
    QCOMPARE(info.titleBar.buttons.at(0).toStdString(), std::string("close"));
    QCOMPARE(info.titleBar.buttons.at(1).toStdString(), std::string("maximize"));

    bool warned = false;
    for (const QString& warning : info.strListWarnings)
    {
        if (warning.contains(QStringLiteral("bogus")))
            warned = true;
    }
    QVERIFY(warned);
#else
    // 非 Windows 原生标题栏，TitleBar 区域跳过，自绘控件不生成。
    QVERIFY(!info.titleBar.bPresent);
#endif

    // 最小尺寸由 window 字段驱动。
    QCOMPARE(info.nMinWidth, 800);
    QCOMPARE(info.nMinHeight, 600);
}

// 关闭标题栏时必须连同区域一起移除，否则会留下一条空白条。
void AssemblyPlannerTest::disabledTitleBarRemovesRegion()
{
    const QByteArray json = R"json({
      "version": 2,
      "profileId": "tb",
      "titleBar": { "enabled": false },
      "regions": [ { "id": "titlebar", "type": "TitleBar" } ]
    })json";

    const auto model = parse(json);
    QVERIFY(model.has_value());

    const AssemblyInfo info = planAssembly(*model, FeatureFlagSet());

    QVERIFY(!info.titleBar.bPresent);
    QVERIFY(findRegion(info, QStringLiteral("titlebar")) == nullptr);
}

// Windows 无 TitleBar 区域 → 强制原生（没有自绘控件可画）；其它平台一直原生。
void AssemblyPlannerTest::titleBarRegionIgnoredOnNonWindows()
{
    const QByteArray json = R"json({
      "version": 2,
      "profileId": "no-custom-bar",
      "regions": [
        { "id": "menubar", "type": "MenuBar", "order": 5 },
        { "id": "center", "type": "CenterView", "order": 30 }
      ]
    })json";

    const auto model = parse(json);
    QVERIFY(model.has_value());

    const AssemblyInfo info = planAssembly(*model, FeatureFlagSet());

    // 没有 TitleBar 区域 → 无论什么平台都强制原生（没有自绘控件可画）。
    QVERIFY(info.bNativeTitleBar);
    QVERIFY(!info.titleBar.bPresent);
    QVERIFY(findRegion(info, QStringLiteral("menubar")) != nullptr);
}

// 菜单栏形态由 profile 的 window.nativeMenuBar 决定：省略 → 窗口内菜单栏（false）。
void AssemblyPlannerTest::nativeMenuBarFollowsProfile()
{
    const QByteArray base = R"json({
      "version": 2,
      "profileId": "menu-bar",
      "regions": [ { "id": "menubar", "type": "MenuBar" } ]
    })json";

    const auto model = parse(base);
    QVERIFY(model.has_value());
    QVERIFY(!model->window.nativeMenuBar.has_value());

    // 省略：默认窗口内菜单栏（菜单项图标可见）。
    AssemblyInfo info = planAssembly(*model, FeatureFlagSet());
    QVERIFY(!info.bNativeMenuBar);

    // 显式 true：交给系统原生菜单栏（macOS 屏幕顶部，无图标）。
    QByteArray json = base;
    json.replace("\"profileId\": \"menu-bar\"", "\"profileId\": \"menu-bar\", \"window\": { \"nativeMenuBar\": true }");
    const auto nativeModel = parse(json);
    QVERIFY(nativeModel.has_value());
    QVERIFY(nativeModel->window.nativeMenuBar.value_or(false));
    info = planAssembly(*nativeModel, FeatureFlagSet());
    QVERIFY(info.bNativeMenuBar);

    // 显式 false：仍为窗口内菜单栏。
    json.replace("\"nativeMenuBar\": true", "\"nativeMenuBar\": false");
    const auto inWindowModel = parse(json);
    QVERIFY(inWindowModel.has_value());
    QVERIFY(!inWindowModel->window.nativeMenuBar.value_or(true));
    info = planAssembly(*inWindowModel, FeatureFlagSet());
    QVERIFY(!info.bNativeMenuBar);
}

// structureKey 反映平台固定的标题栏模式，不反映 profile 里没有的 nativeTitleBar 配置。
void AssemblyPlannerTest::structureKeyReflectsPlatformTitleBarMode()
{
    const auto build = [&](const QByteArray& json) {
        const auto model = parse(json);
        if (!model.has_value())
            return AssemblyInfo{};
        return planAssembly(*model, FeatureFlagSet());
    };

    // base：Windows 自绘 + TitleBar 区域 → bNativeTitleBar=false → titleBar.present=true
    //        非 Windows 原生 + TitleBar 区域 → bNativeTitleBar=true → titleBar.present=false
    const QByteArray baseJson = R"json({
      "version": 2,
      "profileId": "sk",
      "titleBar": { "title": "test", "buttons": [ "close" ] },
      "regions": [
        { "id": "titlebar", "type": "TitleBar", "order": 1 },
        { "id": "left", "type": "LeftDock", "order": 20,
          "areas": [ { "id": "left.main", "preferredWidth": 200 } ] },
        { "id": "center", "type": "CenterView", "order": 30 }
      ],
      "panels": [
        { "id": "panel.a", "factory": "test.panel", "area": "left.main",
          "props": { "mode": "cut" } }
      ],
      "menus": [
        { "type": "submenu", "id": "file", "titleKey": "menu.file", "order": 10,
          "children": [ "file.new" ] }
      ]
    })json";

    AssemblyInfo base = build(baseJson);
    const QString baseKey = base.structureKey();
    QVERIFY(!baseKey.isEmpty());

    // —— 不影响结构 → 键相同 ——

    // 1) 只改菜单 title（文字/key 不进 structureKey）
    QByteArray json = baseJson;
    json.replace("menu.file", "menu.file.bogus");
    QCOMPARE(build(json).structureKey(), baseKey);

    // 2) 只改面板 props（结构键不关心 props 内容）
    json = baseJson;
    json.replace("\"mode\": \"cut\"", "\"mode\": \"paste\"");
    QCOMPARE(build(json).structureKey(), baseKey);

    // —— 影响结构 → 键不同 ——

    // 3) 新增面板
    json = baseJson;
    json.replace("],\n      \"menus\"",
        "{ \"id\": \"panel.b\", \"factory\": \"test.panel\", "
        "\"area\": \"left.main\" } ],\n      \"menus\"");
    QVERIFY(build(json).structureKey() != baseKey);
}

QTEST_GUILESS_MAIN(AssemblyPlannerTest)

#include "tstAssemblyPlanner.moc"
