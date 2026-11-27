#include <QtTest/QtTest>

#include <QString>

#include "Model/Profile/ProfileParser.h"
#include "Model/Profile/ProfileLoadResult.h"
#include "Model/Profile/ProfileSchema.h"

using namespace RD;

namespace
{

    bool hasErrorContaining(const QList<Diagnostic>& diagnostics, const QString& needle)
    {
        for (const Diagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.level == Diagnostic::Level::Error
                && diagnostic.strMsg.contains(needle))
            {
                return true;
            }
        }
        return false;
    }

    bool hasWarningContaining(const QList<Diagnostic>& diagnostics, const QString& needle)
    {
        for (const Diagnostic& diagnostic : diagnostics)
        {
            if (diagnostic.level == Diagnostic::Level::Warning
                && diagnostic.strMsg.contains(needle))
            {
                return true;
            }
        }
        return false;
    }
} // namespace

class ProfileLoaderTest : public QObject
{
    Q_OBJECT

private slots:
    void parsesMinimalProfileAndGeneratesAutoAreas();
    void rejectsUnsupportedVersion();
    void rejectsMissingVersion();
    void rejectsUnknownRegionType();
    void rejectsUnknownPanelArea();
    void rejectsDuplicatePanelId();
    void rejectsMissingProfileId();
    void rejectsInvalidJson();
    void parsesTitleBarAndWindow();
    void parsesBrandMetadata();
    void rejectsInvalidBrandType();
    void titleBarDefaultsWhenAbsent();
    void warnsUnknownTopLevelField();
    void rejectsOversizedProfile();
    void rejectsTooManyPanels();
    void rejectsDeepMenu();
};

// 最小可用 profile：只声明区域，子区域按 "<区域>.main" 自动补全，面板可直接挂载。
void ProfileLoaderTest::parsesMinimalProfileAndGeneratesAutoAreas()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p1",
  "regions": [
    { "id": "left", "type": "LeftDock" },
    { "id": "center", "type": "CenterView" }
  ],
  "panels": [
    { "id": "tools", "factory": "test.panel", "area": "left.main" }
  ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(result.isOk());
    QVERIFY(result.profileModel.has_value());

    const ProfileModel& model = *result.profileModel;
    QCOMPARE(model.strProfileId.toStdString(), std::string("p1"));
    QCOMPARE(model.regions.size(), 2);

    // 自动子区域只在 CenterView 上给伸展权重 1，其余区域固定为 0，避免侧栏抢占空间。
    QCOMPARE(model.regions.at(0).paneAreas.size(), 1);
    QCOMPARE(model.regions.at(0).paneAreas.at(0).strId.toStdString(), std::string("left.main"));
    QCOMPARE(model.regions.at(0).paneAreas.at(0).stretch, 0);

    QCOMPARE(model.regions.at(1).paneAreas.size(), 1);
    QCOMPARE(model.regions.at(1).paneAreas.at(0).strId.toStdString(), std::string("center.main"));
    QCOMPARE(model.regions.at(1).paneAreas.at(0).stretch, 1);

    QCOMPARE(model.panels.size(), 1);
    QCOMPARE(model.panels.at(0).paneAreaId.toStdString(), std::string("left.main"));
}

void ProfileLoaderTest::rejectsUnsupportedVersion()
{
    const QByteArray json = R"json({
  "version": 3,
  "profileId": "p",
  "regions": [ { "id": "left", "type": "LeftDock" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("version")));
    // 版本过新要明确说明“来自更新版本”，而不是笼统报字段非法。
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("newer")));
}

void ProfileLoaderTest::rejectsMissingVersion()
{
    // 缺失版本按“旧格式”拒绝，绝不默认成当前版本后继续解释字段。
    const QByteArray json = R"json({
  "profileId": "p",
  "regions": [ { "id": "center", "type": "CenterView" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("version")));
}

void ProfileLoaderTest::rejectsUnknownRegionType()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p",
  "regions": [ { "id": "x", "type": "Sidebar" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("unknown region type")));
}

void ProfileLoaderTest::rejectsUnknownPanelArea()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p",
  "regions": [ { "id": "left", "type": "LeftDock" } ],
  "panels": [ { "id": "tools", "factory": "test.panel", "area": "left.none" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("unknown area")));
}

// 面板 id 是运行时查找面板的键，重复会让挂载结果依赖遍历顺序，必须在加载期拒绝。
void ProfileLoaderTest::rejectsDuplicatePanelId()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p",
  "regions": [ { "id": "left", "type": "LeftDock" } ],
  "panels": [
    { "id": "tools", "factory": "test.panel", "area": "left.main" },
    { "id": "tools", "factory": "test.panel", "area": "left.main" }
  ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("duplicate panel id")));
}

void ProfileLoaderTest::rejectsMissingProfileId()
{
    const QByteArray json = R"json({
  "version": 2,
  "regions": [ { "id": "left", "type": "LeftDock" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("profileId is required")));
}

// 语法错误的输入必须转成诊断返回，而不是抛异常或静默产出空模型。
void ProfileLoaderTest::rejectsInvalidJson()
{
    const ProfileLoadResult result = ProfileParser::loadFromJson(QByteArray("{ not json"));
    QVERIFY(!result.isOk());
    QVERIFY(!result.diagnostics.isEmpty());
}

void ProfileLoaderTest::parsesTitleBarAndWindow()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p",
  "titleBar": {
    "title": "\u5de5\u4f5c\u53f0",
    "icon": "◆",
    "buttons": [ "close", "maximize", "minimize" ],
    "height": 44,
    "showTitle": false,
    "backgroundColor": "#112233",
    "textColor": "#FFFFFF"
  },
  "window": { "minWidth": 960, "minHeight": 640 },
  "regions": [ { "id": "titlebar", "type": "TitleBar" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(result.isOk());
    QVERIFY(result.profileModel.has_value());

    const ProfileModel& model = *result.profileModel;
    QCOMPARE(model.titleBar.strTitle.toStdString(), std::string("\u5de5\u4f5c\u53f0"));
    QCOMPARE(model.titleBar.strIcon.toStdString(), std::string("◆"));
    QCOMPARE(model.titleBar.listButtons.size(), 3);
    QCOMPARE(model.titleBar.listButtons.at(0).toStdString(), std::string("close"));
    QCOMPARE(model.titleBar.listButtons.at(1).toStdString(), std::string("maximize"));
    QCOMPARE(model.titleBar.listButtons.at(2).toStdString(), std::string("minimize"));
    QCOMPARE(model.titleBar.nHeight, 44);
    QVERIFY(!model.titleBar.bShowTitle);
    QCOMPARE(model.titleBar.strBkgColor.toStdString(), std::string("#112233"));
    // window 段只做原样读取，最小尺寸的夹取留给 AssemblyPlanner，加载器不擅自改数据。
    QCOMPARE(model.window.minWidth, 960);
    QCOMPARE(model.window.minHeight, 640);
}

void ProfileLoaderTest::parsesBrandMetadata()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p",
  "brand": {
    "product": "Site Product",
    "organization": "Site Org",
    "icon": "assets/logo.svg",
    "theme": "DARK",
    "language": "zh_CN",
    "copyright": "Copyright Site",
    "website": "https://site.example",
    "email": "support@site.example",
    "license": "Site license",
    "logo": "assets/logo.svg"
  },
  "regions": [ { "id": "center", "type": "CenterView" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(result.isOk());
    QVERIFY(result.profileModel.has_value());

    const BrandSpec& brand = result.profileModel->brand;
    QCOMPARE(brand.strProduct, QStringLiteral("Site Product"));
    QCOMPARE(brand.strOrganization, QStringLiteral("Site Org"));
    QCOMPARE(brand.strIcon, QStringLiteral("assets/logo.svg"));
    QCOMPARE(brand.strTheme, QStringLiteral("dark"));
    QCOMPARE(brand.strLanguage, QStringLiteral("zh_CN"));
    QCOMPARE(brand.strCopyright, QStringLiteral("Copyright Site"));
    QCOMPARE(brand.strWebsite, QStringLiteral("https://site.example"));
    QCOMPARE(brand.strEmail, QStringLiteral("support@site.example"));
    QCOMPARE(brand.strLicense, QStringLiteral("Site license"));
    QCOMPARE(brand.strLogo, QStringLiteral("assets/logo.svg"));
}

void ProfileLoaderTest::rejectsInvalidBrandType()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p",
  "brand": "not-an-object",
  "regions": [ { "id": "center", "type": "CenterView" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("must be an object")));
}

// 未声明 titleBar 时保持可用默认：启用、可拖拽、三个按钮、800x600 最小尺寸。
void ProfileLoaderTest::titleBarDefaultsWhenAbsent()
{
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "p",
  "regions": [ { "id": "center", "type": "CenterView" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(result.isOk());
    QVERIFY(result.profileModel.has_value());

    const ProfileModel& model = *result.profileModel;
    QVERIFY(model.titleBar.bEnabled);
    QVERIFY(model.titleBar.strTitle.isEmpty());
    QCOMPARE(model.titleBar.listButtons.size(), 3);
    QCOMPARE(model.window.minWidth, 800);
    QCOMPARE(model.window.minHeight, 600);
}

void ProfileLoaderTest::warnsUnknownTopLevelField()
{
    // 未知顶层字段只告警：为未来 schema 增字段保留前向兼容，
    // 同时让拼写错误在诊断里可见。
    const QByteArray json = R"json({
  "version": 2,
  "profileId": "future",
  "futureSection": { "enabled": true },
  "regions": [ { "id": "center", "type": "CenterView" } ]
})json";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(result.isOk());
    QVERIFY(hasWarningContaining(result.diagnostics, QStringLiteral("futureSection")));
}

void ProfileLoaderTest::rejectsOversizedProfile()
{
    // 超过 4 MiB 的输入在 JSONC/解析前就被拒绝，避免复制多份大文本。
    const QByteArray json(ProfileLimits::maxProfileBytes + 1, ' ');
    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("maximum size")));
}

void ProfileLoaderTest::rejectsTooManyPanels()
{
    QByteArray json = "{\"version\":2,\"profileId\":\"big\","
        "\"regions\":[{\"id\":\"center\",\"type\":\"CenterView\"}],\"panels\":[";
    for (int i = 0; i <= ProfileLimits::maxPanels; ++i)
    {
        if (i > 0)
            json += ',';
        json += "{\"id\":\"p" + QByteArray::number(i)
            + "\",\"factory\":\"test.panel\",\"area\":\"center.main\"}";
    }
    json += "]}";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("too many panels")));
}

void ProfileLoaderTest::rejectsDeepMenu()
{
    // 递归解析必须有深度上限；这里构造 maxMenuDepth + 2 层嵌套。
    QByteArray json = "{\"version\":2,\"profileId\":\"deep\","
        "\"regions\":[{\"id\":\"center\",\"type\":\"CenterView\"}],\"menus\":[";
    for (int i = 0; i <= ProfileLimits::maxMenuDepth; ++i)
    {
        json += "{\"type\":\"submenu\",\"id\":\"m" + QByteArray::number(i)
            + "\",\"titleKey\":\"x\",\"children\":[";
    }
    json += "\"file.new\"";
    for (int i = 0; i <= ProfileLimits::maxMenuDepth; ++i)
        json += "]}";
    json += "]}";

    const ProfileLoadResult result = ProfileParser::loadFromJson(json);
    QVERIFY(!result.isOk());
    QVERIFY(hasErrorContaining(result.diagnostics, QStringLiteral("depth")));
}

QTEST_GUILESS_MAIN(ProfileLoaderTest)

#include "tstProfileLoader.moc"