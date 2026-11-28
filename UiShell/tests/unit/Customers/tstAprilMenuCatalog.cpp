#include <QtTest/QtTest>

#include <QByteArray>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include "commands/Commands.h"

namespace
{
    int countCommandNodes(const QJsonArray& nodes)
    {
        int count = 0;
        for (const QJsonValue& value : nodes)
        {
            if (!value.isObject())
                continue;
            const QJsonObject obj = value.toObject();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("command"))
                ++count;
            const QJsonValue children = obj.value(QStringLiteral("children"));
            if (children.isArray())
                count += countCommandNodes(children.toArray());
        }
        return count;
    }

    void verifyCommandsRegistered(const Apr::Commands& commands, const QJsonArray& nodes)
    {
        for (const QJsonValue& value : nodes)
        {
            if (!value.isObject())
                continue;
            const QJsonObject obj = value.toObject();
            if (obj.value(QStringLiteral("type")).toString() == QStringLiteral("command"))
            {
                const QString id = obj.value(QStringLiteral("commandId")).toString();
                QVERIFY2(!id.isEmpty(), "command node without commandId");
                QVERIFY2(commands.find(id) != nullptr, qPrintable(QStringLiteral("missing command: %1").arg(id)));
            }
            const QJsonValue children = obj.value(QStringLiteral("children"));
            if (children.isArray())
                verifyCommandsRegistered(commands, children.toArray());
        }
    }
}  // namespace

class AprilMenuCatalogTest : public QObject
{
    Q_OBJECT

private slots:
    void registersNestedMenuCommandsWithMetadata();
    void shippedProfileRegistersEveryMenuCommand();
    void repeatedLoadDoesNotDuplicate();
    void interfaceRegistersExternalProfileCommand();
};

void AprilMenuCatalogTest::registersNestedMenuCommandsWithMetadata()
{
    const QByteArray json = R"({
        "version": 2,
        "menus": [
            { "type": "submenu", "id": "file", "children": [
                { "type": "command", "commandId": "file.new", "titleKey": "New", "iconKey": ":/new.svg" },
                { "type": "separator" },
                { "type": "submenu", "id": "recent", "children": [
                    { "type": "command", "commandId": "file.open_recent", "titleKey": "Recent" }
                ] }
            ] }
        ]
    })";

    Apr::Commands commands;
    QCOMPARE(commands.addCommandsFromProfile(json), 2);

    const AA::CommandView* first = commands.find(QStringLiteral("file.new"));
    QVERIFY(first != nullptr);
    QCOMPARE(first->strTitleKey, QStringLiteral("New"));
    QCOMPARE(first->strIconKey, QStringLiteral(":/new.svg"));

    QVERIFY(commands.find(QStringLiteral("file.open_recent")) != nullptr);
    QVERIFY(commands.find(QStringLiteral("file")) == nullptr);      // 菜单 id 不是命令
    QVERIFY(commands.find(QStringLiteral("unknown.command")) == nullptr);
}

void AprilMenuCatalogTest::shippedProfileRegistersEveryMenuCommand()
{
    QFile file(QString::fromLatin1(APRIL_PROFILE_PATH));
    QVERIFY2(file.open(QIODevice::ReadOnly), "cannot open shipped April ui.json");

    const QByteArray json = file.readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    QVERIFY(doc.isObject());
    const QJsonArray menus = doc.object().value(QStringLiteral("menus")).toArray();
    QVERIFY(menus.size() > 0);

    Apr::Commands commands;
    QCOMPARE(commands.addCommandsFromProfile(json), countCommandNodes(menus));
    verifyCommandsRegistered(commands, menus);
}

void AprilMenuCatalogTest::repeatedLoadDoesNotDuplicate()
{
    QFile file(QString::fromLatin1(APRIL_PROFILE_PATH));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray json = file.readAll();

    Apr::Commands commands;
    const int first = commands.addCommandsFromProfile(json);
    QVERIFY(first > 0);
    QCOMPARE(commands.addCommandsFromProfile(json), 0);
    QCOMPARE(commands.commands().size(), first);
}

void AprilMenuCatalogTest::interfaceRegistersExternalProfileCommand()
{
    // core 只看到 ICommandProfileRegistrar，不认识 Apr::Commands。
    const QByteArray json = R"({
        "version": 2,
        "menus": [
            { "type": "submenu", "id": "site", "children": [
                { "type": "command", "commandId": "site.external.command", "titleKey": "Site command" }
            ] }
        ]
    })";

    Apr::Commands commands;
    AA::ICommandProfileRegistrar* registrar = &commands;
    QCOMPARE(registrar->registerFromProfile(json), 1);
    const AA::CommandView* view = commands.find(QStringLiteral("site.external.command"));
    QVERIFY(view != nullptr);
    QCOMPARE(view->strTitleKey, QStringLiteral("Site command"));
    QCOMPARE(registrar->registerFromProfile(json), 0);
}

QTEST_GUILESS_MAIN(AprilMenuCatalogTest)

#include "tstAprilMenuCatalog.moc"
