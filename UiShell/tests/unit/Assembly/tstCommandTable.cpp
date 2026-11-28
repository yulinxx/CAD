#include <QtTest/QtTest>

#include <QJsonObject>
#include <QStringList>

#include "UiShell/Contract/CommandTable.h"

using namespace AA;

namespace
{
    // 客户命令类的写法：在构造函数里登记全部命令。
    class SampleCommands final : public CommandTable
    {
    public:
        SampleCommands()
        {
            add(QStringLiteral("file.new"), [this](const QJsonObject& args) {
                calls.append(QStringLiteral("file.new"));
                lastArgs = args;
                hasDocument = true;
            }).shortcut(QStringLiteral("Ctrl+N"));

            add(QStringLiteral("file.save"))
                .icon(QStringLiteral(":/icons/save.png"))
                .toolTip(QStringLiteral("tip.save"))
                .needsFeatures(QStringList{ QStringLiteral("io") })
                .enabledWhen([this]() { return hasDocument; });

            add(QStringLiteral("view.grid"), [this](const QJsonObject&) { grid = !grid; })
                .title(QStringLiteral("custom.grid"))
                .checkedWhen([this]() { return grid; });

            add(QStringLiteral("file.print"), [this](const QJsonObject&) { calls.append(QStringLiteral("file.print")); })
                .visibleWhen([this]() { return hasDocument; });
        }

        QStringList calls;
        QJsonObject lastArgs;
        bool hasDocument = false;
        bool grid = false;
    };
}  // namespace

// CommandTable：命令目录 + 执行 + 状态的一张表，菜单 / 工具栏 / 面板 / 快捷键都经它的三个接口调用。
class CommandTableTest : public QObject
{
    Q_OBJECT

private slots:
    void registersViewWithDefaults();
    void fluentSettersApplyToLastCommand();
    void invokeRunsHandlerWithArgs();
    void unknownOrHandlerlessCommandIsIgnored();
    void queryStateOnlyChangesProvidedFields();
    void visibleStateFollowsProvider();
    void addingSameIdReplaces();
};

void CommandTableTest::registersViewWithDefaults()
{
    SampleCommands commands;
    const CommandView* view = commands.find(QStringLiteral("file.new"));
    QVERIFY(view != nullptr);
    QCOMPARE(view->strId, QStringLiteral("file.new"));
    QCOMPARE(view->strTitleKey, QStringLiteral("command.file.new"));  // 默认 "command.<id>"
    QCOMPARE(view->strShortcut, QStringLiteral("Ctrl+N"));
    QVERIFY(!view->bCheckable);
    QVERIFY(commands.find(QStringLiteral("missing")) == nullptr);
}

void CommandTableTest::fluentSettersApplyToLastCommand()
{
    SampleCommands commands;
    const CommandView* save = commands.find(QStringLiteral("file.save"));
    QVERIFY(save != nullptr);
    QCOMPARE(save->strIconKey, QStringLiteral(":/icons/save.png"));
    QCOMPARE(save->strToolTipKey, QStringLiteral("tip.save"));
    QCOMPARE(save->listRequiredFeatures, QStringList{ QStringLiteral("io") });
    QVERIFY(save->strShortcut.isEmpty());  // file.new 的快捷键没有串到 file.save 上

    const CommandView* grid = commands.find(QStringLiteral("view.grid"));
    QVERIFY(grid != nullptr);
    QCOMPARE(grid->strTitleKey, QStringLiteral("custom.grid"));
    QVERIFY(grid->bCheckable);  // checkedWhen 同时标记为开关类命令
}

void CommandTableTest::invokeRunsHandlerWithArgs()
{
    SampleCommands commands;
    commands.invokeWithArgs(QStringLiteral("file.new"), QJsonObject{ { QStringLiteral("target"), 9 } });
    QCOMPARE(commands.calls, QStringList{ QStringLiteral("file.new") });
    QCOMPARE(commands.lastArgs.value(QStringLiteral("target")).toInt(), 9);

    // 不带参数的 invoke 按空参数执行同一个处理函数。
    commands.invoke(QStringLiteral("file.new"));
    QCOMPARE(commands.calls.size(), 2);
    QVERIFY(commands.lastArgs.isEmpty());
}

void CommandTableTest::unknownOrHandlerlessCommandIsIgnored()
{
    SampleCommands commands;
    commands.invoke(QStringLiteral("missing"));
    commands.invoke(QStringLiteral("file.save"));  // 登记了但没有处理函数
    QVERIFY(commands.calls.isEmpty());
}

// status 进来时是 shell 的当前值：没有登记条件的字段原样返回。
void CommandTableTest::queryStateOnlyChangesProvidedFields()
{
    SampleCommands commands;

    CommandStatus save;
    commands.queryState(QStringLiteral("file.save"), save);
    QVERIFY(!save.bEnabled);  // 还没有文档

    commands.invoke(QStringLiteral("file.new"));
    save = CommandStatus();
    commands.queryState(QStringLiteral("file.save"), save);
    QVERIFY(save.bEnabled);

    CommandStatus grid;
    grid.bChecked = true;  // shell 当前值
    commands.queryState(QStringLiteral("view.grid"), grid);
    QVERIFY(!grid.bChecked);  // 以命令表为准
    QVERIFY(grid.bEnabled);   // 没有 enabledWhen：保持原值

    CommandStatus untouched;
    untouched.bEnabled = false;
    untouched.bChecked = true;
    commands.queryState(QStringLiteral("file.new"), untouched);
    QVERIFY(!untouched.bEnabled);
    QVERIFY(untouched.bChecked);

    commands.queryState(QStringLiteral("missing"), untouched);  // 未知命令：不改
    QVERIFY(untouched.bChecked);
}

// 可见性是命令状态的一部分：菜单 / 工具栏 / 右键 / 面板共用动作时不能再各存一份。
void CommandTableTest::visibleStateFollowsProvider()
{
    SampleCommands commands;
    CommandStatus status;
    commands.queryState(QStringLiteral("file.print"), status);
    QVERIFY(!status.bVisible);

    commands.invoke(QStringLiteral("file.new"));
    status = CommandStatus();
    commands.queryState(QStringLiteral("file.print"), status);
    QVERIFY(status.bVisible);
}

void CommandTableTest::addingSameIdReplaces()
{
    class Replacing final : public CommandTable
    {
    public:
        Replacing()
        {
            add(QStringLiteral("a"), [this](const QJsonObject&) { ++first; }).shortcut(QStringLiteral("F1"));
            add(QStringLiteral("a"), [this](const QJsonObject&) { ++second; });
        }
        int first = 0;
        int second = 0;
    } commands;

    commands.invoke(QStringLiteral("a"));
    QCOMPARE(commands.first, 0);
    QCOMPARE(commands.second, 1);
    QVERIFY(commands.find(QStringLiteral("a"))->strShortcut.isEmpty());
}

QTEST_GUILESS_MAIN(CommandTableTest)

#include "tstCommandTable.moc"
