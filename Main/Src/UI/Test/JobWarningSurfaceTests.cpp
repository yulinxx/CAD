/**
 * @file JobWarningSurfaceTests.cpp
 * @brief 告警链路回归：ProcessingJobService 的 jobWarning / jobFinished(false)
 *        必须在生产 UI 里真的被监听并展示出来。
 *
 * 背景：修复前全仓只有单测连过 jobWarning，生产代码里没有任何监听者。
 * LaserRoute 报告「运动卡只落实首段激光参数」时，界面上仍显示一次成功的加工，
 * 属于典型的静默降级。DevicePanel 现在是这条链路的展示位。
 */
#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QString>

#include "Hardware/ProcessingJobService.h"
#include "UI/Widgets/DevicePanel.h"

namespace
{
// MainTests 用 unity build：多个 .cpp 会拼进同一个 TU，匿名命名空间里的同名
// 辅助函数会互相重定义（EmergencyStopTests.cpp / DevicePanelHardwareOffTests.cpp
// 都有 ensureApplication）。这里必须用专属名字。
QApplication* ensureJobWarningTestApplication()
{
    if (qApp)
    {
        return qobject_cast<QApplication*>(qApp);
    }

    static int argc = 1;
    static char argv0[] = "MainTests";
    static char* argv[] = { argv0, nullptr };
    static QApplication app(argc, argv);
    return &app;
}
} // namespace

TEST(JobWarningSurfaceTests, JobWarningReachesOperator)
{
    ensureJobWarningTestApplication();

    ProcessingJobService service(nullptr);
    DevicePanel panel(nullptr, &service, nullptr, nullptr);

    auto* warning = panel.findChild<QLabel*>(QStringLiteral("devicePanelWarning"));
    ASSERT_NE(warning, nullptr);
    EXPECT_TRUE(warning->isHidden());
    EXPECT_TRUE(warning->text().isEmpty());

    emit service.jobWarning(
        QStringLiteral("Only the first laser parameter group was applied to the laser source"));

    EXPECT_FALSE(warning->isHidden());
    EXPECT_TRUE(warning->text().contains(QStringLiteral("Warning")));
    EXPECT_TRUE(warning->text().contains(QStringLiteral("Only the first laser parameter group")));
}

TEST(JobWarningSurfaceTests, FailedJobMessageIsShownAndClearedOnNextStart)
{
    ensureJobWarningTestApplication();

    ProcessingJobService service(nullptr);
    DevicePanel panel(nullptr, &service, nullptr, nullptr);

    auto* warning = panel.findChild<QLabel*>(QStringLiteral("devicePanelWarning"));
    ASSERT_NE(warning, nullptr);

    emit service.jobFinished(false, QStringLiteral("Emission enable refused"));
    EXPECT_FALSE(warning->isHidden());
    EXPECT_TRUE(warning->text().contains(QStringLiteral("Emission enable refused")));

    // 新作业开始时旧告警必须消失，避免操作员把上一次的告警误当成本次的问题
    emit service.jobStarted(QStringLiteral("job-1"), 3);
    EXPECT_TRUE(warning->isHidden());
    EXPECT_TRUE(warning->text().isEmpty());

    // 成功结束不产生告警
    emit service.jobFinished(true, QString());
    EXPECT_TRUE(warning->isHidden());
}
