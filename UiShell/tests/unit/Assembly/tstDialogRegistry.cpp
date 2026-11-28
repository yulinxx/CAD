#include <QtTest/QtTest>

#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>
#include <QString>

#include "UiShell/Contract/DialogRegistry.h"

using namespace AA;

namespace
{
    // 带编号的对话框工厂替身：openDialog 把编号放进结果，便于区分命中的是哪个实例。
    struct FakeDialogFactory final : IDialogFactory
    {
        explicit FakeDialogFactory(int number)
            : number(number)
        {
        }

        DialogResult openDialog(QWidget*, const QJsonObject&) override
        {
            DialogResult result;
            result.status = DialogResult::Accepted;
            result.data.insert(QStringLiteral("number"), number);
            return result;
        }

        int number = 0;
    };
}  // namespace

// 对话框注册表：dialogId → IDialogFactory*，同 id 后注册覆盖先注册，无效绑定被忽略。
class DialogRegistryTest : public QObject
{
    Q_OBJECT

private slots:
    void findsRegisteredFactory();
    void unknownIdReturnsNull();
    void laterRegistrationOverrides();
    void ignoresInvalidBindings();
};

void DialogRegistryTest::findsRegisteredFactory()
{
    ApiPtr<IDialogFactoryRegistry> registry(createDialogRegistry());
    QVERIFY(registry);

    FakeDialogFactory factory(1);
    registry->registerFactory(QStringLiteral("dialog.a"), &factory);
    QCOMPARE(registry->find(QStringLiteral("dialog.a")), static_cast<IDialogFactory*>(&factory));
}

void DialogRegistryTest::unknownIdReturnsNull()
{
    ApiPtr<IDialogFactoryRegistry> registry(createDialogRegistry());
    QVERIFY(registry);
    QVERIFY(registry->find(QStringLiteral("dialog.missing")) == nullptr);
}

// 后注册覆盖先注册：客户 / 共享库以同一 id 注册时，以后注册的为准。
void DialogRegistryTest::laterRegistrationOverrides()
{
    ApiPtr<IDialogFactoryRegistry> registry(createDialogRegistry());
    QVERIFY(registry);

    FakeDialogFactory first(1);
    FakeDialogFactory second(2);
    registry->registerFactory(QStringLiteral("dialog.a"), &first);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("overridden")));
    registry->registerFactory(QStringLiteral("dialog.a"), &second);

    IDialogFactory* found = registry->find(QStringLiteral("dialog.a"));
    QCOMPARE(found, static_cast<IDialogFactory*>(&second));
    QCOMPARE(found->openDialog(nullptr, QJsonObject()).data.value(QStringLiteral("number")).toInt(), 2);
}

void DialogRegistryTest::ignoresInvalidBindings()
{
    ApiPtr<IDialogFactoryRegistry> registry(createDialogRegistry());
    QVERIFY(registry);

    FakeDialogFactory factory(1);
    registry->registerFactory(QString(), &factory);
    registry->registerFactory(QStringLiteral("dialog.b"), nullptr);

    QVERIFY(registry->find(QString()) == nullptr);
    QVERIFY(registry->find(QStringLiteral("dialog.b")) == nullptr);
}

QTEST_GUILESS_MAIN(DialogRegistryTest)

#include "tstDialogRegistry.moc"
