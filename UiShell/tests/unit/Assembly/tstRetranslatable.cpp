#include <QtTest/QtTest>

#include <QLabel>

#include <QStringList>

#include "UiShell/Contract/Retranslatable.h"

using namespace RD;

// 三个客户原先各有一份 Retranslatable/markPanel；下沉到 contract 后
// 这里锁住公共语义：回调按注册顺序执行、角色/系列写入稳定属性。
class RetranslatableTest : public QObject
{
    Q_OBJECT

private slots:
    void retranslateRunsRefreshersInOrder();
    void markPanelSetsStableProperties();
};

void RetranslatableTest::retranslateRunsRefreshersInOrder()
{
    Retranslatable<QLabel> label;
    QStringList calls;

    label.onRetranslate([&calls]() { calls.append(QStringLiteral("first")); });
    label.onRetranslate([&calls]() { calls.append(QStringLiteral("second")); });

    QVERIFY(calls.isEmpty());

    label.retranslate();
    QCOMPARE(calls, (QStringList{ QStringLiteral("first"), QStringLiteral("second") }));

    // 重复调用每次都执行，不做一次性消费。
    label.retranslate();
    QCOMPARE(calls.size(), 4);
}

void RetranslatableTest::markPanelSetsStableProperties()
{
    QLabel label;

    markPanel(&label, QStringLiteral("panelTitle"), QStringLiteral("a"));

    QCOMPARE(label.property("msuiRole").toString(), QStringLiteral("panelTitle"));
    QCOMPARE(label.property("msuiSeries").toString(), QStringLiteral("a"));
}

QTEST_MAIN(RetranslatableTest)

#include "tstRetranslatable.moc"
