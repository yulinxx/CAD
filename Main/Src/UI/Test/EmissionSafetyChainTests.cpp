/**
 * @file EmissionSafetyChainTests.cpp
 * @brief 出光安全链回归测试：视觉标定打点不得绕过安全裁决与出光守卫，
 *        急停在任何状态下都必须关光并闭锁。
 *
 * 这些用例锁的是「旁路」而不是设备行为：断言的是打点请求在到达
 * Hw::IGalvoCard::loadPlan/startMark 之前就被拒绝，且出光使能不会
 * 被留在 Enabled。设备自身的联锁由 HardwareTests 覆盖。
 *
 * 分层：
 *   - EmissionGuard / 离线急停：两种构建都能跑（ENABLE_HARDWARE 无关）。
 *   - 视觉标定桥：需要 ENABLE_VISION + ENABLE_HARDWARE + 模拟设备。
 */

#include <gtest/gtest.h>

#include <limits>

#include <QString>

#include "Hardware/DeviceHost.h"
#include "Hardware/EmissionGuard.h"

#if defined(ENABLE_VISION) && defined(ENABLE_HARDWARE)
    #include "Vision/Core/VisionTypes.h"
    #include "Vision/VisionHostBridges.h"
    #include "Hardware/MachineProfile.h"
#endif

namespace
{
    // ==================== 出光守卫状态机 ====================

    /// 急停闭锁后必须显式确认才能再次开光，且确认动作本身不开光。
    TEST(EmissionSafetyChain, LatchedGuardRefusesBeginJobUntilExplicitRearm)
    {
        EmissionGuard guard;
        EXPECT_EQ(EmissionState::Off, guard.state());

        guard.emergencyStop(QStringLiteral("unitTest"));
        EXPECT_EQ(EmissionState::Latched, guard.state());
        EXPECT_FALSE(guard.isEmissionPermitted());

        QString error;
        EXPECT_FALSE(guard.beginJob(error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_EQ(EmissionState::Latched, guard.state());

        // endJob 是正常收工，不得顺手把急停闭锁解掉
        guard.endJob();
        EXPECT_EQ(EmissionState::Latched, guard.state());

        EXPECT_TRUE(guard.confirmRearm(error)) << error.toStdString();
        EXPECT_EQ(EmissionState::Off, guard.state());
        EXPECT_FALSE(guard.isEmissionPermitted()) << "confirmRearm must not enable emission by itself";

        EXPECT_TRUE(guard.beginJob(error)) << error.toStdString();
        EXPECT_EQ(EmissionState::Enabled, guard.state());
        guard.endJob();
        EXPECT_EQ(EmissionState::Off, guard.state());
    }

    /// 第二次 beginJob 必须拒绝并发作业，否则两处作业会各自认为「我关了光」。
    TEST(EmissionSafetyChain, BeginJobRefusesConcurrentJobs)
    {
        EmissionGuard guard;
        QString error;
        ASSERT_TRUE(guard.beginJob(error)) << error.toStdString();

        EXPECT_FALSE(guard.beginJob(error));
        EXPECT_EQ(EmissionState::Enabled, guard.state());

        guard.endJob();
        EXPECT_EQ(EmissionState::Off, guard.state());
    }

    /// abort / endJob 幂等，且 abort 不能把 Latched 洗成 Off。
    TEST(EmissionSafetyChain, AbortIsIdempotentAndPreservesLatch)
    {
        EmissionGuard guard;
        QString error;
        ASSERT_TRUE(guard.beginJob(error)) << error.toStdString();

        guard.abort(QStringLiteral("unitTestAbort"));
        guard.abort(QStringLiteral("unitTestAbortAgain"));
        guard.endJob();
        EXPECT_EQ(EmissionState::Off, guard.state());

        guard.emergencyStop(QStringLiteral("unitTestEstop"));
        guard.abort(QStringLiteral("unitTestAbortWhileLatched"));
        EXPECT_EQ(EmissionState::Latched, guard.state());
    }

    // ==================== 离线急停 ====================

    /// 设备没启动时按急停不能是「只记日志」：必须闭锁出光并置上软件急停，
    /// 否则「离线按急停 → 开机 → 无需确认即可出光」。
    TEST(EmissionSafetyChain, OfflineEmergencyStopLatchesEmission)
    {
        DeviceHost host;
        ASSERT_FALSE(host.isRunning());
        ASSERT_EQ(EmissionState::Off, host.emissionGuard().state());

        host.requestEmergencyStop();

        EXPECT_EQ(EmissionState::Latched, host.emissionGuard().state());
        EXPECT_FALSE(host.emissionGuard().isEmissionPermitted());
        EXPECT_TRUE(host.isSoftwareEmergencyActive());
        EXPECT_FALSE(host.canStartProcessing());

        QString error;
        EXPECT_FALSE(host.emissionGuard().beginJob(error));
    }

    /// 闭锁必须由操作员显式复位；复位后仍不自动开光。
    TEST(EmissionSafetyChain, OfflineLatchNeedsExplicitAckAndDoesNotEnableLaser)
    {
        DeviceHost host;
        host.requestEmergencyStop();

        host.clearSoftwareEmergencyStop();
        EXPECT_FALSE(host.isSoftwareEmergencyActive());

        QString error;
        EXPECT_TRUE(host.acknowledgeEmissionLatch(error)) << error.toStdString();
        EXPECT_EQ(EmissionState::Off, host.emissionGuard().state());
        EXPECT_FALSE(host.emissionGuard().isEmissionPermitted());

        // 复位本身不开光，仍要走 beginJob
        EXPECT_TRUE(host.emissionGuard().beginJob(error)) << error.toStdString();
        host.emissionGuard().endJob();
        EXPECT_EQ(EmissionState::Off, host.emissionGuard().state());
    }

#if defined(ENABLE_VISION) && defined(ENABLE_HARDWARE)

    // ==================== 视觉标定打点桥 ====================

    /// 让安全裁决处于「允许开工」：兜底档案的安全门点位有 20ms 去抖，
    /// 需要两拍才认（与 HardwareAssemblyTests 里的时序一致）。
    void allowProcessing(DeviceHost& host)
    {
        host.tick(20);
        host.tick(20);
    }

    /// 未做过安全评估时 canStartProcessing() 恒为 false；
    /// 标定打点必须在此之前就被拒绝，不能下发任何计划。
    TEST(EmissionSafetyChain, VisionMarkingRefusedBeforeSafetyVerdictAllowsIt)
    {
        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(MachineProfileLoader::builtinSimulatedProfile(), error)) << error.toStdString();
        ASSERT_FALSE(host.canStartProcessing());

        VisHost::GalvoController galvo(&host);
        const Vis::VisResult result = galvo.markPoint(1.0, 2.0);

        EXPECT_TRUE(result.failed());
        EXPECT_EQ(Vis::VisError::InvalidState, result.error);
        EXPECT_EQ(EmissionState::Off, host.emissionGuard().state())
            << "refused marking must not leave emission enabled";
        EXPECT_FALSE(host.emissionGuard().isEmissionPermitted());

        host.stop();
    }

    /// 安全裁决本已放行，但出光被急停/安全链闭锁：
    /// 门控可能读到上一拍缓存，beginJob 这道必须拦住。
    TEST(EmissionSafetyChain, VisionMarkingRefusedWhenEmissionIsLatched)
    {
        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(MachineProfileLoader::builtinSimulatedProfile(), error)) << error.toStdString();
        allowProcessing(host);
        ASSERT_TRUE(host.canStartProcessing()) << host.safetyViolations().join(';').toStdString();

        // 只闭锁出光守卫，不刷新安全裁决缓存：模拟「安全链刚关光、
        // 裁决还没跑到下一拍」这个最容易漏的窗口。
        // 用 fault() 而不是 safetyCutoff()：后者在 Off 态刻意不闭锁
        // （启动阶段例行关光不该把机器锁死），而 fault()/emergencyStop()
        // 即使当时没在出光也要求操作员显式确认。
        host.emissionGuard().fault(QStringLiteral("unitTestFault"));
        ASSERT_EQ(EmissionState::Latched, host.emissionGuard().state());

        VisHost::GalvoController galvo(&host);
        const Vis::VisResult result = galvo.markPattern(nullptr, 0);
        EXPECT_EQ(Vis::VisError::InvalidArgument, result.error);  // 参数校验先于安全门

        const Vis::VisResult mark = galvo.markPoint(3.0, 4.0);
        EXPECT_TRUE(mark.failed());
        EXPECT_EQ(Vis::VisError::InvalidState, mark.error);
        EXPECT_EQ(EmissionState::Latched, host.emissionGuard().state());

        host.stop();
    }

    /// 运行中急停必须立刻把裁决缓存置为不可开工，并闭锁出光。
    TEST(EmissionSafetyChain, RunningEmergencyStopRevokesPermissionAndLatches)
    {
        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(MachineProfileLoader::builtinSimulatedProfile(), error)) << error.toStdString();
        allowProcessing(host);
        ASSERT_TRUE(host.canStartProcessing());

        host.requestEmergencyStop();

        EXPECT_FALSE(host.canStartProcessing())
            << "canStartProcessing must not serve a stale verdict after an E-stop";
        EXPECT_EQ(EmissionState::Latched, host.emissionGuard().state());
        EXPECT_TRUE(host.isSoftwareEmergencyActive());

        VisHost::GalvoController galvo(&host);
        EXPECT_TRUE(galvo.markPoint(0.0, 0.0).failed());

        host.clearSoftwareEmergencyStop();
        EXPECT_FALSE(host.isSoftwareEmergencyActive());
        host.stop();
    }

    /// 非法坐标仍按参数错误返回，且不能因此打开出光使能。
    TEST(EmissionSafetyChain, VisionMarkingRejectsNonFiniteCoordinatesWithoutEmission)
    {
        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(MachineProfileLoader::builtinSimulatedProfile(), error)) << error.toStdString();
        allowProcessing(host);
        ASSERT_TRUE(host.canStartProcessing());

        Vis::PointF points[2] = {};
        points[0].x = 1.0;
        points[0].y = 1.0;
        points[1].x = std::numeric_limits<double>::quiet_NaN();
        points[1].y = 2.0;

        VisHost::GalvoController galvo(&host);
        const Vis::VisResult result = galvo.markPattern(points, 2);

        EXPECT_EQ(Vis::VisError::InvalidArgument, result.error);
        EXPECT_EQ(EmissionState::Off, host.emissionGuard().state());

        host.stop();
    }

#endif  // ENABLE_VISION && ENABLE_HARDWARE
}  // namespace
