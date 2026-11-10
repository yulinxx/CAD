/**
 * @file EmergencyStopTests.cpp
 * @brief 急停界面状态机与常驻入口验证：触发、物理复位、复位确认与开光分离。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QStringList>

#include <memory>

#include "UI/Safety/EmergencyStopController.h"
#include "UI/Widgets/EmergencyStopOverlay.h"

namespace
{
QApplication* ensureEmergencyApplication()
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

class FakeDeviceState
{
public:
    std::unique_ptr<EmergencyStopController> createController()
    {
        return std::make_unique<EmergencyStopController>(
            [this]() { return persistedLatch; }, [this](bool latched) { persistedLatch = latched; });
    }

    bool persistedLatch{ false };
    bool monitoring{ true };
    bool inputActive{ false };
};
}  // namespace

TEST(EmergencyStopTests, PhysicalResetIsRequiredBeforeConfirmReset)
{
    ensureEmergencyApplication();

    FakeDeviceState device;
    auto controller = device.createController();
    controller->setDeviceStateProviders(
        [&device]() { return device.monitoring; }, [&device]() { return device.inputActive; });

    controller->triggerEmergencyStop();
    controller->pollDeviceState();

    EXPECT_EQ(controller->state(), EmergencyStopController::State::EmergencyStopped);
    EXPECT_TRUE(controller->isReadOnly());
    EXPECT_FALSE(controller->canConfirmReset());

    // 急停输入从未按下，不能仅凭“读不到”就判定已物理复位
    device.inputActive = true;
    controller->pollDeviceState();
    EXPECT_FALSE(controller->canConfirmReset());

    // 物理按钮释放后才算复位完成
    device.inputActive = false;
    controller->pollDeviceState();
    EXPECT_TRUE(controller->isPhysicalResetObserved());
    EXPECT_TRUE(controller->canConfirmReset());
}

TEST(EmergencyStopTests, ConfirmResetOnlyClearsLatchAndDoesNotEnableLaser)
{
    ensureEmergencyApplication();

    FakeDeviceState device;
    auto controller = device.createController();
    controller->setDeviceStateProviders(
        [&device]() { return device.monitoring; }, [&device]() { return device.inputActive; });

    controller->triggerEmergencyStop();
    device.inputActive = true;
    controller->pollDeviceState();
    device.inputActive = false;
    controller->pollDeviceState();

    ASSERT_TRUE(controller->confirmReset());
    EXPECT_EQ(controller->state(), EmergencyStopController::State::ResetConfirmed);
    EXPECT_FALSE(controller->isReadOnly());
    EXPECT_FALSE(controller->isPhysicalResetObserved());
    EXPECT_FALSE(device.persistedLatch);
}

TEST(EmergencyStopTests, PersistedLatchSurvivesRestartAsEmergencyState)
{
    ensureEmergencyApplication();

    FakeDeviceState device;
    device.persistedLatch = true;

    auto controller = device.createController();
    controller->setDeviceStateProviders(
        [&device]() { return device.monitoring; }, [&device]() { return device.inputActive; });

    EXPECT_EQ(controller->state(), EmergencyStopController::State::EmergencyStopped);
    EXPECT_TRUE(controller->isReadOnly());
}

TEST(EmergencyStopTests, OverlayTriggersImmediatelyAndKeepsTwoSeparateActions)
{
    ensureEmergencyApplication();

    FakeDeviceState device;
    auto controller = device.createController();
    controller->setDeviceStateProviders(
        [&device]() { return device.monitoring; }, [&device]() { return device.inputActive; });

    int emergencyRequests = 0;
    int startRequests = 0;
    EmergencyStopOverlay overlay(
        controller.get(), [&emergencyRequests]() { ++emergencyRequests; }, [&startRequests]() { ++startRequests; });

    EXPECT_TRUE(overlay.windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    EXPECT_TRUE(overlay.emergencyButton()->styleSheet().contains(QStringLiteral("#c62828")));
    EXPECT_FALSE(overlay.safetyNoticeLabel()->isHidden());
    EXPECT_TRUE(overlay.stepsLabel()->isHidden());

    // 单击即触发，没有二次确认
    overlay.emergencyButton()->click();
    EXPECT_EQ(controller->state(), EmergencyStopController::State::EmergencyStopped);
    EXPECT_EQ(emergencyRequests, 1);
    EXPECT_FALSE(overlay.stepsLabel()->isHidden());
    EXPECT_FALSE(overlay.confirmResetButton()->isEnabled());
    EXPECT_FALSE(overlay.startLaserButton()->isEnabled());

    device.inputActive = true;
    controller->pollDeviceState();
    EXPECT_FALSE(overlay.confirmResetButton()->isEnabled());

    device.inputActive = false;
    controller->pollDeviceState();
    EXPECT_TRUE(overlay.confirmResetButton()->isEnabled());

    overlay.confirmResetButton()->click();
    EXPECT_EQ(controller->state(), EmergencyStopController::State::ResetConfirmed);
    EXPECT_EQ(startRequests, 0) << "confirm reset must not emit laser on";
    EXPECT_TRUE(overlay.startLaserButton()->isEnabled());

    // 开光是独立的第二次动作
    overlay.startLaserButton()->click();
    EXPECT_EQ(startRequests, 1);
}

TEST(EmergencyStopTests, RestartLatchKeepsStartActionDisabled)
{
    ensureEmergencyApplication();

    FakeDeviceState device;
    device.persistedLatch = true;
    auto controller = device.createController();
    controller->setDeviceStateProviders(
        [&device]() { return device.monitoring; }, [&device]() { return device.inputActive; });

    int startRequests = 0;
    EmergencyStopOverlay overlay(controller.get(), []() {}, [&startRequests]() { ++startRequests; });

    EXPECT_EQ(controller->state(), EmergencyStopController::State::EmergencyStopped);
    EXPECT_FALSE(overlay.startLaserButton()->isEnabled());
}
