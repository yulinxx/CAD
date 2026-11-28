#include <QtTest/QtTest>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include "Assembly/Runtime/ProfilePipeline.h"
#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/CommandTable.h"

using namespace AA;

namespace
{
    QByteArray profileWithMenuCommand(const QString& commandId)
    {
        QJsonArray regions{
            QJsonObject{ { QStringLiteral("id"), QStringLiteral("menubar") },
                { QStringLiteral("type"), QStringLiteral("MenuBar") },
                { QStringLiteral("order"), 5 } }
        };
        QJsonArray menus{
            QJsonObject{ { QStringLiteral("type"), QStringLiteral("submenu") },
                { QStringLiteral("id"), QStringLiteral("site") },
                { QStringLiteral("titleKey"), QStringLiteral("Site") },
                { QStringLiteral("children"), QJsonArray{
                    QJsonObject{ { QStringLiteral("type"), QStringLiteral("command") },
                        { QStringLiteral("commandId"), commandId },
                        { QStringLiteral("titleKey"), QStringLiteral("Custom") } } } } }
        };

        QJsonObject root;
        root.insert(QStringLiteral("version"), 2);
        root.insert(QStringLiteral("profileId"), QStringLiteral("site.profile"));
        root.insert(QStringLiteral("regions"), regions);
        root.insert(QStringLiteral("menus"), menus);
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    bool hasWarningContaining(const QList<Diagnostic>& diagnostics, const QString& needle)
    {
        for (const Diagnostic& d : diagnostics)
        {
            if (d.level == Diagnostic::Level::Warning && d.strMsg.contains(needle))
                return true;
        }
        return false;
    }
}  // namespace

class ProfileCommandsTest : public QObject
{
    Q_OBJECT

private slots:
    void withoutRegistrarUnknownCommandIsFiltered();
    void registrarAddsCommandsBeforePlanning();
    void registrationIsIdempotent();
};

void ProfileCommandsTest::withoutRegistrarUnknownCommandIsFiltered()
{
    CommandTable catalog;
    ShellServices services;
    services.cmdCatalog = &catalog;
    services.cmdProfileRegistrar = nullptr;

    ProfilePipeline pipeline;
    QVERIFY(pipeline.loadFromJson(profileWithMenuCommand(QStringLiteral("site.custom.command")), services));
    QVERIFY(!catalog.find(QStringLiteral("site.custom.command")));
    QVERIFY(hasWarningContaining(pipeline.diagnostics(), QStringLiteral("unknown command")));
}

void ProfileCommandsTest::registrarAddsCommandsBeforePlanning()
{
    CommandTable catalog;
    ShellServices services;
    services.cmdCatalog = &catalog;
    services.cmdProfileRegistrar = &catalog;

    ProfilePipeline pipeline;
    QVERIFY(pipeline.loadFromJson(profileWithMenuCommand(QStringLiteral("site.custom.command")), services));

    const CommandView* view = catalog.find(QStringLiteral("site.custom.command"));
    QVERIFY2(view != nullptr, "external profile command must be registered");
    QCOMPARE(view->strTitleKey, QStringLiteral("Custom"));
    QVERIFY(!hasWarningContaining(pipeline.diagnostics(), QStringLiteral("unknown command")));
}

void ProfileCommandsTest::registrationIsIdempotent()
{
    CommandTable catalog;
    const QByteArray json = profileWithMenuCommand(QStringLiteral("site.custom.command"));
    QCOMPARE(catalog.registerFromProfile(json), 1);
    QCOMPARE(catalog.registerFromProfile(json), 0);
    QVERIFY(catalog.find(QStringLiteral("site.custom.command")) != nullptr);
}

QTEST_GUILESS_MAIN(ProfileCommandsTest)

#include "tstProfileCommands.moc"
