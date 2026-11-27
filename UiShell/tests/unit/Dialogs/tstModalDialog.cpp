#include <QtTest/QtTest>

#include <QApplication>
#include <QDialog>
#include <QJsonObject>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include "UiShell/Contract/ModalDialog.h"

using namespace RD;

// runModalDialog：所有对话框工厂（框架默认、共享库、TyvokStudio / GoodLaser）共用的打开方式。
// 锁住四种结局：没打开、确定、取消、打开期间父窗口被销毁。
class ModalDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void nullDialogReturnsCancelled();
    void acceptedCollectsDataAndDeletesDialog();
    void rejectedSkipsCollect();
    void parentDestroyedWhileOpenIsSafe();
    void markDialogSetsRoleAndSeries();
};

void ModalDialogTest::nullDialogReturnsCancelled()
{
    const DialogResult result = runModalDialog(static_cast<QDialog*>(nullptr));
    QCOMPARE(int(result.status), int(DialogResult::Cancelled));
    QVERIFY(result.data.isEmpty());
}

void ModalDialogTest::acceptedCollectsDataAndDeletesDialog()
{
    auto* dialog = new QDialog();
    QPointer<QDialog> watch(dialog);
    // exec() 先 show 再进事件循环：0 ms 定时器在对话框可见后触发。
    QTimer::singleShot(0, dialog, [dialog]() { dialog->accept(); });

    bool collected = false;
    const DialogResult result = runModalDialog(dialog, [&collected](QDialog*) {
        collected = true;
        return QJsonObject{ { QStringLiteral("value"), 7 } };
    });

    QVERIFY(collected);
    QCOMPARE(int(result.status), int(DialogResult::Accepted));
    QCOMPARE(result.data.value(QStringLiteral("value")).toInt(), 7);
    QVERIFY(watch.isNull());  // 返回前已释放
}

void ModalDialogTest::rejectedSkipsCollect()
{
    auto* dialog = new QDialog();
    QPointer<QDialog> watch(dialog);
    QTimer::singleShot(0, dialog, [dialog]() { dialog->reject(); });

    bool collected = false;
    const DialogResult result = runModalDialog(dialog, [&collected](QDialog*) {
        collected = true;
        return QJsonObject();
    });

    QVERIFY(!collected);
    QCOMPARE(int(result.status), int(DialogResult::Rejected));
    QVERIFY(result.data.isEmpty());
    QVERIFY(watch.isNull());
}

// 回归：exec() 期间父窗口被销毁（如功能开关变化触发重新装配）时，Qt 会连带删除对话框。
// 栈上对话框在这里会重复释放；runModalDialog 必须按 Rejected 返回，且不再访问对话框。
void ModalDialogTest::parentDestroyedWhileOpenIsSafe()
{
    auto* parent = new QWidget();
    auto* dialog = new QDialog(parent);
    QPointer<QDialog> watch(dialog);

    QTimer::singleShot(0, qApp, [parent = QPointer<QWidget>(parent)]() { delete parent.data(); });

    bool collected = false;
    const DialogResult result = runModalDialog(dialog, [&collected](QDialog*) {
        collected = true;
        return QJsonObject();
    });

    QVERIFY(!collected);
    QCOMPARE(int(result.status), int(DialogResult::Rejected));
    QVERIFY(watch.isNull());
}

// markDialog：皮肤选择器 QDialog[msuiRole="dialog"][msuiSeries="<系列>"] 依赖这两个动态属性。
void ModalDialogTest::markDialogSetsRoleAndSeries()
{
    QDialog branded;
    QCOMPARE(markDialog(&branded, QJsonObject{ { QString::fromLatin1(DialogParam::Series), QStringLiteral("b") } }),
        &branded);
    QCOMPARE(branded.property("msuiRole").toString(), QStringLiteral("dialog"));
    QCOMPARE(branded.property("msuiSeries").toString(), QStringLiteral("b"));

    // 没有系列：仍是通用对话框配色。
    QDialog generic;
    markDialog(&generic, QJsonObject());
    QCOMPARE(generic.property("msuiRole").toString(), QStringLiteral("dialog"));
    QVERIFY(generic.property("msuiSeries").toString().isEmpty());

    QVERIFY(markDialog(static_cast<QDialog*>(nullptr), QJsonObject()) == nullptr);
}

QTEST_MAIN(ModalDialogTest)

#include "tstModalDialog.moc"
