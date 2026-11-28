#include <QtTest/QtTest>

#include <QFile>
#include <QJsonArray>
#include <QStringList>

#include "Model/Profile/ProfileParser.h"
#include "Model/Profile/ProfileLoadResult.h"

using namespace AA;

namespace
{

    ProfileLoadResult loadCustomer(const QString& name)
    {
        const QString path = QString::fromUtf8(UI_CUSTOMERS_DIR)
            + QLatin1Char('/') + name + QStringLiteral("/ui.json");

        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return ProfileLoadResult{};
        return ProfileParser::loadFromJson(file.readAll());
    }

    std::string errorText(const ProfileLoadResult& result)
    {
        if (result.diagnostics.isEmpty())
            return "no diagnostics";
        return result.diagnostics.first().strMsg.toStdString();
    }

    int indexOfRegion(const ProfileModel& model, const QString& regionId)
    {
        for (int i = 0; i < model.regions.size(); ++i)
        {
            if (model.regions.at(i).strId == regionId)
                return i;
        }
        return -1;
    }

    const PanelSpec* findPanel(const ProfileModel& model, const QString& panelId)
    {
        for (const PanelSpec& panel : model.panels)
        {
            if (panel.strId == panelId)
                return &panel;
        }
        return nullptr;
    }
} // namespace

class CustomerProfileTest : public QObject
{
    Q_OBJECT

private slots:
    void mantiSoftProfileIsValid();
    void tyvokStudioProfileIsValid();
    void goodLaserProfileIsValid();
    void titleBarConfigsDiffer();
    void aprilProfileRestoresToolbarsAndNativeMenu();
};

// 三个客户配置都必须能被框架的通用解析器接受；
// 差异只体现在数据（区域/子区域/菜单）上，而不是靠客户代码分支。
void CustomerProfileTest::mantiSoftProfileIsValid()
{
    const ProfileLoadResult result = loadCustomer(QStringLiteral("MantiSoft"));
    QVERIFY2(result.isOk(), errorText(result).c_str());
    QVERIFY(result.profileModel.has_value());

    const ProfileModel& model = *result.profileModel;
    QCOMPARE(model.strProfileId.toStdString(), std::string("mantisoft"));

    // MantiSoft：左右两侧都有，顶部单区，底部单区。
    const int left = indexOfRegion(model, QStringLiteral("left"));
    const int right = indexOfRegion(model, QStringLiteral("right"));
    QVERIFY(left >= 0);
    QVERIFY(right >= 0);
    QCOMPARE(model.regions.at(left).paneAreas.size(), 1);
    QCOMPARE(model.regions.at(right).paneAreas.size(), 1);

    // 菜单按 order 升序排列，配置里的书写顺序不影响最终顺序。
    QCOMPARE(model.menus.size(), 9);
    QCOMPARE(model.menus.at(0).strId.toStdString(), std::string("file"));
    QCOMPARE(model.menus.at(1).strId.toStdString(), std::string("edit"));
    QCOMPARE(model.menus.at(8).strId.toStdString(), std::string("help"));
}

void CustomerProfileTest::tyvokStudioProfileIsValid()
{
    const ProfileLoadResult result = loadCustomer(QStringLiteral("TyvokStudio"));
    QVERIFY2(result.isOk(), errorText(result).c_str());
    QVERIFY(result.profileModel.has_value());

    const ProfileModel& model = *result.profileModel;
    QCOMPARE(model.strProfileId.toStdString(), std::string("tyvokstudio"));

       // TyvokStudio：没有左区；右侧上下分栏；顶部左右分栏。
    QCOMPARE(indexOfRegion(model, QStringLiteral("left")), -1);
    const int right = indexOfRegion(model, QStringLiteral("right"));
    QVERIFY(right >= 0);
    QCOMPARE(model.regions.at(right).paneAreas.size(), 2);

    const int top = indexOfRegion(model, QStringLiteral("top"));
    QVERIFY(top >= 0);
    QCOMPARE(model.regions.at(top).paneAreas.size(), 2);

    QCOMPARE(model.menus.size(), 4);
    QCOMPARE(model.menus.at(0).strId.toStdString(), std::string("project"));
    QCOMPARE(model.menus.at(1).strId.toStdString(), std::string("machining"));
}

void CustomerProfileTest::goodLaserProfileIsValid()
{
    const ProfileLoadResult result = loadCustomer(QStringLiteral("GoodLaser"));
    QVERIFY2(result.isOk(), errorText(result).c_str());
    QVERIFY(result.profileModel.has_value());

    const ProfileModel& model = *result.profileModel;
    QCOMPARE(model.strProfileId.toStdString(), std::string("goodlaser"));

       // GoodLaser：没有右区；左侧上/中(QML 面板)/下三段；底部左右分格。
    QCOMPARE(indexOfRegion(model, QStringLiteral("right")), -1);
    const int left = indexOfRegion(model, QStringLiteral("left"));
    QVERIFY(left >= 0);
    QCOMPARE(model.regions.at(left).paneAreas.size(), 3);

    const int bottom = indexOfRegion(model, QStringLiteral("bottom"));
    QVERIFY(bottom >= 0);
    QCOMPARE(model.regions.at(bottom).paneAreas.size(), 2);

    QCOMPARE(model.menus.size(), 5);
    QCOMPARE(model.menus.at(0).strId.toStdString(), std::string("design"));
    QCOMPARE(model.menus.at(2).strId.toStdString(), std::string("analysis"));
    // GoodLaser 通过 help.about 接入与 MantiSoft / TyvokStudio 相同的关于对话框链路（界面由 GoodLaser 自己注册）。
    QCOMPARE(model.menus.at(4).strId.toStdString(), std::string("help"));
}

// 三套标题栏必须来自 profile，且集合/顺序/高度确实不同。
void CustomerProfileTest::titleBarConfigsDiffer()
{
    const ProfileLoadResult a = loadCustomer(QStringLiteral("MantiSoft"));
    const ProfileLoadResult b = loadCustomer(QStringLiteral("TyvokStudio"));
    const ProfileLoadResult c = loadCustomer(QStringLiteral("GoodLaser"));
    QVERIFY2(a.isOk(), errorText(a).c_str());
    QVERIFY2(b.isOk(), errorText(b).c_str());
    QVERIFY2(c.isOk(), errorText(c).c_str());

    QVERIFY(indexOfRegion(*a.profileModel, QStringLiteral("titlebar")) >= 0);
    QVERIFY(indexOfRegion(*b.profileModel, QStringLiteral("titlebar")) >= 0);
    QVERIFY(indexOfRegion(*c.profileModel, QStringLiteral("titlebar")) >= 0);

    // 三套高度 26/30/44 各不相同，证明标题栏外观由 profile 决定而不是硬编码。
    QCOMPARE(a.profileModel->titleBar.nHeight, 26);
    QCOMPARE(b.profileModel->titleBar.nHeight, 30);
    QCOMPARE(c.profileModel->titleBar.nHeight, 44);

    QCOMPARE(a.profileModel->titleBar.listButtons.size(), 3);
    QCOMPARE(a.profileModel->titleBar.listButtons.at(0).toStdString(), std::string("minimize"));
    QCOMPARE(a.profileModel->titleBar.listButtons.at(2).toStdString(), std::string("close"));

       // TyvokStudio：没有最大化按钮。
    QCOMPARE(b.profileModel->titleBar.listButtons.size(), 2);
    QCOMPARE(b.profileModel->titleBar.listButtons.at(0).toStdString(), std::string("minimize"));
    QCOMPARE(b.profileModel->titleBar.listButtons.at(1).toStdString(), std::string("close"));

       // GoodLaser：按钮顺序（最小化→最大化→关闭）和 MantiSoft 一致，但高度 44 不同，
    // 证明顺序来自 profile 配置而不是代码硬编码。
    QCOMPARE(c.profileModel->titleBar.listButtons.size(), 3);
    QCOMPARE(c.profileModel->titleBar.listButtons.at(0).toStdString(), std::string("minimize"));
    QCOMPARE(c.profileModel->titleBar.listButtons.at(1).toStdString(), std::string("maximize"));
    QCOMPARE(c.profileModel->titleBar.listButtons.at(2).toStdString(), std::string("close"));
}

// April production shell must restore both toolbars and use the macOS native menu bar.
// These are profile data, so the generic parser test is enough to guard them.
void CustomerProfileTest::aprilProfileRestoresToolbarsAndNativeMenu()
{
    const ProfileLoadResult result = loadCustomer(QStringLiteral("April"));
    QVERIFY2(result.isOk(), errorText(result).c_str());
    QVERIFY(result.profileModel.has_value());

    const ProfileModel& model = *result.profileModel;
    QVERIFY(model.window.nativeMenuBar.has_value());
    QVERIFY(*model.window.nativeMenuBar);

    const PanelSpec* drawTools = findPanel(model, QStringLiteral("drawTools"));
    QVERIFY(drawTools != nullptr);
    QCOMPARE(drawTools->paneAreaId.toStdString(), std::string("left.main"));
    QCOMPARE(drawTools->props.value(QStringLiteral("kind")).toString().toStdString(), std::string("drawTools"));

    const PanelSpec* editToolbar = findPanel(model, QStringLiteral("editToolbar"));
    QVERIFY(editToolbar != nullptr);
    QCOMPARE(editToolbar->paneAreaId.toStdString(), std::string("top.toolbar"));
    QCOMPARE(editToolbar->props.value(QStringLiteral("kind")).toString().toStdString(), std::string("topToolbar"));

    const QJsonArray commands = editToolbar->props.value(QStringLiteral("commands")).toArray();
    QVERIFY(commands.contains(QStringLiteral("edit.undo")));
    QVERIFY(commands.contains(QStringLiteral("edit.redo")));

    // Existing panels remain mounted; the toolbars are additive.
    QVERIFY(findPanel(model, QStringLiteral("scene")) != nullptr);
    QVERIFY(findPanel(model, QStringLiteral("inspector")) != nullptr);
    QVERIFY(findPanel(model, QStringLiteral("status")) != nullptr);

    // Top-level menu order comes from profile order, not from id alphabetical order.
    // Without order the parser sorts by id and Draw/Edit jump ahead of File.
    const QStringList expectedMenus{ QStringLiteral("file"), QStringLiteral("edit"),
        QStringLiteral("view"), QStringLiteral("draw"), QStringLiteral("modify"),
        QStringLiteral("algorithm"), QStringLiteral("laser"), QStringLiteral("help") };
    QCOMPARE(model.menus.size(), expectedMenus.size());
    for (int i = 0; i < expectedMenus.size(); ++i)
        QCOMPARE(model.menus.at(i).strId, expectedMenus.at(i));
}

QTEST_GUILESS_MAIN(CustomerProfileTest)

#include "tstCustomerProfile.moc"
