#include <QtTest/QtTest>

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

#include <cmath>
#include <functional>

#include "UiDialogs/SampleDialogs.h"
#include "UiDialogs/SampleSolver.h"

using namespace AA;
using namespace AA::samples;

namespace
{
    // 模态 exec() 会阻塞：在对话框自己的事件循环里找到它并执行 action；没找到就稍后重试。
    void whenDialogShown(const QString& objectName, std::function<void(QDialog*)> action, int attempts = 200)
    {
        QTimer::singleShot(10, qApp, [objectName, action, attempts]() {
            for (QWidget* widget : QApplication::topLevelWidgets())
            {
                if (widget->objectName() != objectName || !widget->isVisible())
                    continue;
                if (auto* dialog = qobject_cast<QDialog*>(widget))
                {
                    action(dialog);
                    return;
                }
            }
            if (attempts > 0)
                whenDialogShown(objectName, action, attempts - 1);
        });
    }
}  // namespace

// 示例「参数对话框 → 命令 → 算法」：算法纯函数、参数 JSON 往返、默认对话框收集参数。
class SampleDialogsTest : public QObject
{
    Q_OBJECT

private slots:
    void newtonConverges();
    void bisectionConverges();
    void iterationLimitStopsEarly();
    void negativeTargetIsInvalid();
    void paramsFromJsonClampsAndDefaults();
    void paramsRoundTrip();
    void dialogReturnsEditedValues();
    void dialogRestrictsMethods();
    void cancelReturnsRejected();
    void otherCommandsAreNotHandled();
    void dialogUsesStyleSeries();
};

void SampleDialogsTest::newtonConverges()
{
    SampleParams params;
    params.target = 2.0;
    const SampleResult result = solveSquareRoot(params);
    QVERIFY(result.valid);
    QVERIFY(result.converged);
    QVERIFY(std::fabs(result.value - std::sqrt(2.0)) < 1e-9);
}

void SampleDialogsTest::bisectionConverges()
{
    SampleParams params;
    params.target = 10.0;
    params.maxIterations = 60;
    params.tolerance = 1e-4;
    params.method = QStringLiteral("bisection");
    const SampleResult result = solveSquareRoot(params);
    QVERIFY(result.converged);
    QVERIFY(std::fabs(result.value - std::sqrt(10.0)) < 1e-4);
}

void SampleDialogsTest::iterationLimitStopsEarly()
{
    SampleParams params;
    params.target = 2.0;
    params.maxIterations = 1;
    const SampleResult result = solveSquareRoot(params);
    QVERIFY(result.valid);
    QVERIFY(!result.converged);
    QCOMPARE(result.iterations, 1);
}

void SampleDialogsTest::negativeTargetIsInvalid()
{
    SampleParams params;
    params.target = -1.0;
    const SampleResult result = solveSquareRoot(params);
    QVERIFY(!result.valid);
    QVERIFY(!result.error.isEmpty());
}

void SampleDialogsTest::paramsFromJsonClampsAndDefaults()
{
    const SampleParams defaults = SampleParams::fromJson(QJsonObject());
    QCOMPARE(defaults.target, 2.0);
    QCOMPARE(defaults.maxIterations, 20);
    QCOMPARE(defaults.method, QStringLiteral("newton"));

    const SampleParams clamped = SampleParams::fromJson(QJsonObject{
        { QStringLiteral("target"), -5.0 },
        { QStringLiteral("maxIterations"), 0 },
        { QStringLiteral("tolerance"), 10.0 },
        { QStringLiteral("method"), QStringLiteral("magic") } });
    QCOMPARE(clamped.target, 0.0);
    QCOMPARE(clamped.maxIterations, SampleLimits::kMinIterations);
    QCOMPARE(clamped.tolerance, SampleLimits::kMaxTolerance);
    QCOMPARE(clamped.method, QStringLiteral("newton"));
}

void SampleDialogsTest::paramsRoundTrip()
{
    SampleParams params;
    params.target = 7.5;
    params.maxIterations = 42;
    params.tolerance = 1e-8;
    params.method = QStringLiteral("bisection");

    const SampleParams back = SampleParams::fromJson(params.toJson());
    QCOMPARE(back.target, params.target);
    QCOMPARE(back.maxIterations, params.maxIterations);
    QCOMPARE(back.tolerance, params.tolerance);
    QCOMPARE(back.method, params.method);
}

// 对话框读 params 作初值，确定后把表单值放进 DialogResult.data。
void SampleDialogsTest::dialogReturnsEditedValues()
{
    const QJsonObject params{ { QStringLiteral("target"), 9.0 },
        { QStringLiteral("maxIterations"), 15 },
        { QStringLiteral("tolerance"), 1e-5 },
        { QStringLiteral("method"), QStringLiteral("bisection") } };

    bool seen = false;
    double initialTarget = 0.0;
    QString initialMethod;
    whenDialogShown(QStringLiteral("sample.params.dialog"), [&](QDialog* dialog) {
        seen = true;
        if (auto* target = dialog->findChild<QDoubleSpinBox*>(QStringLiteral("sample.target")))
        {
            initialTarget = target->value();
            target->setValue(16.0);
        }
        if (auto* method = dialog->findChild<QComboBox*>(QStringLiteral("sample.method")))
            initialMethod = method->currentData().toString();
        dialog->accept();
    });

    const DialogResult result = SampleParamsDialogFactory().openDialog(nullptr, params);
    QVERIFY(seen);
    QCOMPARE(initialTarget, 9.0);
    QCOMPARE(initialMethod, QStringLiteral("bisection"));
    QCOMPARE(int(result.status), int(DialogResult::Accepted));

    const SampleParams values = SampleParams::fromJson(result.data);
    QCOMPARE(values.target, 16.0);
    QCOMPARE(values.maxIterations, 15);
    QCOMPARE(values.method, QStringLiteral("bisection"));
    QVERIFY(std::fabs(values.tolerance - 1e-5) < 1e-12);
}

// params.methods 限制下拉选项：客户差异写在绑定 params 里，不用改代码。
void SampleDialogsTest::dialogRestrictsMethods()
{
    const QJsonObject params{ { QStringLiteral("method"), QStringLiteral("newton") },
        { QStringLiteral("methods"), QJsonArray{ QStringLiteral("bisection") } } };

    int count = -1;
    QString current;
    whenDialogShown(QStringLiteral("sample.params.dialog"), [&](QDialog* dialog) {
        if (auto* method = dialog->findChild<QComboBox*>(QStringLiteral("sample.method")))
        {
            count = method->count();
            current = method->currentData().toString();
        }
        dialog->reject();
    });

    SampleParamsDialogFactory().openDialog(nullptr, params);
    QCOMPARE(count, 1);
    QCOMPARE(current, QStringLiteral("bisection"));
}

void SampleDialogsTest::cancelReturnsRejected()
{
    whenDialogShown(QStringLiteral("sample.params.dialog"), [](QDialog* dialog) { dialog->reject(); });
    const DialogResult result = SampleParamsDialogFactory().openDialog(nullptr, QJsonObject());
    QCOMPARE(int(result.status), int(DialogResult::Rejected));
    QVERIFY(result.data.isEmpty());
}

void SampleDialogsTest::otherCommandsAreNotHandled()
{
    QVERIFY(!runSampleCommand(QStringLiteral("file.save"), QJsonObject()));
}

// 共享对话框不知道自己属于哪个客户：品牌系列来自 params（shell 补进的 DialogParam::Series）。
void SampleDialogsTest::dialogUsesStyleSeries()
{
    QString role;
    QString series;
    whenDialogShown(QStringLiteral("sample.params.dialog"), [&](QDialog* dialog) {
        role = dialog->property("msuiRole").toString();
        series = dialog->property("msuiSeries").toString();
        dialog->reject();
    });

    SampleParamsDialogFactory().openDialog(
        nullptr, QJsonObject{ { QString::fromLatin1(DialogParam::Series), QStringLiteral("c") } });
    QCOMPARE(role, QStringLiteral("dialog"));
    QCOMPARE(series, QStringLiteral("c"));
}

QTEST_MAIN(SampleDialogsTest)

#include "tstSampleDialogs.moc"
