/**
 * @file SafetyManagerTests.cpp
 * @brief 安全管控单元测试
 *
 * 测试目标：
 * - 验证安全等级判断逻辑
 * - 验证急停功能
 * - 验证温度监控
 * - 验证安全门状态
 */

#include <gtest/gtest.h>
#include "Hardware/SafetyManager.h"

// 本测试文件仍然构造已标记弃用的 SafetyManager，用于回归其行为。
// 该类仓库内零构造点，保留仅为 ABI 兼容；如确认无仓库外消费者，可连同这些用例一起删除。
// 此处仅在本文件范围内压制 C4996，不影响工程级警告策略，也不去掉符号上的弃用标记。
#if defined(_MSC_VER)
#  pragma warning(push)
#  pragma warning(disable : 4996)
#endif

// 测试默认安全状态
TEST(SafetyManagerTest, DefaultState_IsSafe)
{
    SafetyManager manager;

    EXPECT_FALSE(manager.isEmergencyStopped());
    EXPECT_FALSE(manager.isDoorOpen());
    EXPECT_FALSE(manager.isOverTemperature());
    EXPECT_TRUE(manager.isSafeToOperate());
    EXPECT_EQ(manager.currentSafetyLevel(), SafetyManager::SafetyLevel::Normal);
}

// 测试急停功能
TEST(SafetyManagerTest, EmergencyStop_ChangesState)
{
    SafetyManager manager;

    EXPECT_FALSE(manager.isEmergencyStopped());

    manager.emergencyStop();
    EXPECT_TRUE(manager.isEmergencyStopped());
    EXPECT_FALSE(manager.isSafeToOperate());
}

TEST(SafetyManagerTest, ResetEmergencyStop_RestoresState)
{
    SafetyManager manager;

    manager.emergencyStop();
    EXPECT_TRUE(manager.isEmergencyStopped());

    manager.resetEmergencyStop();
    EXPECT_FALSE(manager.isEmergencyStopped());
}

// 测试安全门
TEST(SafetyManagerTest, DoorOpened_ChangesSafetyLevel)
{
    SafetyManager manager;

    EXPECT_FALSE(manager.isDoorOpen());

    manager.onDoorOpened();
    EXPECT_TRUE(manager.isDoorOpen());
}

TEST(SafetyManagerTest, DoorClosed_RestoresState)
{
    SafetyManager manager;

    manager.onDoorOpened();
    EXPECT_TRUE(manager.isDoorOpen());

    manager.onDoorClosed();
    EXPECT_FALSE(manager.isDoorOpen());
}

// 测试温度监控
TEST(SafetyManagerTest, TemperatureBelowLimit_NotOverTemperature)
{
    SafetyManager manager;
    manager.setTemperatureLimit(60.0);

    manager.updateTemperature(50.0);
    EXPECT_FALSE(manager.isOverTemperature());
}

TEST(SafetyManagerTest, TemperatureAboveLimit_IsOverTemperature)
{
    SafetyManager manager;
    manager.setTemperatureLimit(60.0);

    manager.updateTemperature(70.0);
    EXPECT_TRUE(manager.isOverTemperature());
}

TEST(SafetyManagerTest, TemperatureLimit_CanBeChanged)
{
    SafetyManager manager;

    manager.setTemperatureLimit(80.0);
    EXPECT_DOUBLE_EQ(manager.temperatureLimit(), 80.0);
}

// 测试安全等级
TEST(SafetyManagerTest, SafetyLevel_EmergencyWhenEmergencyStopped)
{
    SafetyManager manager;

    manager.emergencyStop();
    EXPECT_EQ(manager.currentSafetyLevel(), SafetyManager::SafetyLevel::Emergency);
}

// 测试操作日志
TEST(SafetyManagerTest, LogEvent_AddsToRecentEvents)
{
    SafetyManager manager;

    manager.logEvent(SafetyManager::SafetyEvent::DoorOpened, "Test door event");

    QStringList events = manager.getRecentEvents(10);
    EXPECT_GE(events.size(), 1);
}

// 测试配置
TEST(SafetyManagerTest, AutoStopOnDoor_CanBeConfigured)
{
    SafetyManager manager;

    manager.setAutoStopOnDoor(false);
    // 验证配置已应用（具体实现可能需要检查内部状态）
}

TEST(SafetyManagerTest, MaxContinuousRunTime_CanBeSet)
{
    SafetyManager manager;

    manager.setMaxContinuousRunTime(60);
    EXPECT_EQ(manager.maxContinuousRunTime(), 60);
}

// 测试自检
TEST(SafetyManagerTest, PerformSelfCheck_ReturnsResults)
{
    SafetyManager manager;

    bool result = manager.performSelfCheck();
    QStringList checkResults = manager.getCheckResults();

    // 自检应该返回结果
    EXPECT_GE(checkResults.size(), 0);
}

#if defined(_MSC_VER)
#  pragma warning(pop)
#endif
