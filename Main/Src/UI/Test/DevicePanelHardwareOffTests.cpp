/**
 * @file DevicePanelHardwareOffTests.cpp
 * @brief BUILD_HARDWARE=OFF 语义验证：面板照常显示、控件全禁用、命令给出明确提示。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QStringList>

#include <memory>

#include "Composition/LaserOperationRegistry.h"
#include "Hardware/DeviceHost.h"
#include "UI/Command/OperationId.h"
#include "UI/Widgets/DevicePanel.h"
#include "UI2D/Operation/OperationBus.h"

namespace
{
QApplication* ensureDevicePanelApplication()
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
}  // namespace

TEST(DevicePanelHardwareOffTests, PanelStaysVisibleAndAllControlsDisabled)
{
    ensureDevicePanelApplication();

    if (DeviceHost::isHardwareSupportCompiled())
    {
        GTEST_SKIP() << "this test targets BUILD_HARDWARE=OFF";
    }

    DevicePanel panel(nullptr, nullptr, nullptr, nullptr);
    panel.show();

    const auto buttons = panel.findChildren<QPushButton*>();
    ASSERT_FALSE(buttons.isEmpty());
    for (auto* button : buttons)
    {
        EXPECT_FALSE(button->isEnabled()) << button->objectName().toUtf8().constData();
    }

    auto* hint = panel.findChild<QLabel*>(QStringLiteral("devicePanelHint"));
    ASSERT_NE(hint, nullptr);
    EXPECT_FALSE(hint->isHidden());
    EXPECT_FALSE(hint->text().isEmpty());

    auto* status = panel.findChild<QLabel*>(QStringLiteral("devicePanelStatus"));
    ASSERT_NE(status, nullptr);
    EXPECT_TRUE(status->text().contains(QStringLiteral("not enabled"), Qt::CaseInsensitive));
}

TEST(DevicePanelHardwareOffTests, NullDeviceHostDoesNotCrash)
{
    ensureDevicePanelApplication();

    if (DeviceHost::isHardwareSupportCompiled())
    {
        GTEST_SKIP() << "this test targets BUILD_HARDWARE=OFF";
    }

    DevicePanel panel(nullptr, nullptr, nullptr, nullptr);
    panel.refreshState();

    for (auto* button : panel.findChildren<QPushButton*>())
    {
        EXPECT_FALSE(button->isEnabled());
    }
}

TEST(DevicePanelHardwareOffTests, LaserCommandsStayClickableAndReportDisabledMessage)
{
    ensureDevicePanelApplication();

    if (DeviceHost::isHardwareSupportCompiled())
    {
        GTEST_SKIP() << "this test targets BUILD_HARDWARE=OFF";
    }

    DeviceHost deviceHost;
    OperationBus bus;
    QStringList errors;

    LaserOperationConfig config;
    config.bus = &bus;
    config.deviceHost = &deviceHost;
    config.errorReporter = [&errors](const QString& message) { errors.append(message); };

    LaserOperationRegistry registry(config);
    registry.registerAll();

    const OperationId ids[] = { OperationId::Laser_StartProcess, OperationId::Laser_PauseProcess,
        OperationId::Laser_StopProcess };
    for (const OperationId id : ids)
    {
        IOperation* operation = bus.registry().get(id);
        ASSERT_NE(operation, nullptr);
        EXPECT_TRUE(operation->canExecute()) << "OFF build must keep the command clickable to report a hint";

        const int before = errors.size();
        bus.run(id);
        ASSERT_EQ(errors.size(), before + 1);
        EXPECT_TRUE(errors.last().contains(QStringLiteral("BUILD_HARDWARE=OFF")));
    }
}
