/**
 * @file HardwareAssemblyTests.cpp
 * @brief 硬件装配层测试：机器档案解析 + DeviceHost 装配。
 *
 * 这里断言的是「装配层的契约」，而不是设备本身的行为
 * （后者由 HardwareTests 覆盖，全部跑在 SimulatedDevice 上）。
 *
 * 重点覆盖两类容易在现场出事的路径：
 *   1. 没有档案 / 档案写错时的兜底行为 —— 必须能启动，但必须留下明确警告；
 *   2. 设备起不来时 canStartProcessing 必须为 false（fail-safe），
 *      而不是「没有约束所以放行」。
 */

#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "Hardware/DeviceHost.h"
#include "Hardware/MachineProfile.h"

namespace
{
    /// 把 JSON 文本写进临时目录里的 machine.json，返回完整路径。
    QString writeProfile(const QTemporaryDir& dir, const QByteArray& json)
    {
        const QString path = QDir(dir.path()).filePath(QStringLiteral("machine.json"));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
        {
            return QString();
        }
        file.write(json);
        file.close();
        return path;
    }

    // ==================== 机器档案 ====================

    /// 内置兜底档案必须是「可直接用的模拟机」，而不是空壳。
    TEST(MachineProfileTest, BuiltinFallbackIsUsableSimulatedMachine)
    {
        const MachineProfile p = MachineProfileLoader::builtinSimulatedProfile();

        EXPECT_EQ(QStringLiteral("simulated_composite"), p.deviceId);
        EXPECT_TRUE(p.fromFallback);
        EXPECT_TRUE(p.autoOpen);
        EXPECT_TRUE(p.sourcePath.isEmpty());

        // 兜底档案也要带安全点位与条件：模拟模式下安全链路必须真的在跑，
        // 否则联锁逻辑到真机才第一次被执行
        EXPECT_FALSE(p.ioPoints.isEmpty());
        EXPECT_FALSE(p.safetyConditions.isEmpty());
    }

    /// 急停点位：不允许去抖（去抖是滤机械抖动的，安全信号不接受任何延迟），
    /// 且极性必须是 activeLow —— 常闭接法下「高=正常、低=被按下」。
    /// 极性写反会让模拟机一上电就判定「急停被按下」而永远无法开工。
    TEST(MachineProfileTest, FallbackEmergencyStopPointPolarityAndDebounce)
    {
        const MachineProfile p = MachineProfileLoader::builtinSimulatedProfile();

        bool found = false;
        for (const MachineIoPointConfig& pt : p.ioPoints)
        {
            if (pt.name == QStringLiteral("safety.estop"))
            {
                found = true;
                EXPECT_EQ(0, pt.debounceMs);
                EXPECT_TRUE(pt.activeLow);
            }
        }
        EXPECT_TRUE(found);
    }


    /// 找不到档案时回退，并且必须给出可展示的警告文案（不能静默把模拟当真机）。
    TEST(MachineProfileTest, MissingProfileFallsBackWithWarning)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());

        QString warning;
        const MachineProfile p = MachineProfileLoader::loadOrFallback(dir.path(), warning);

        EXPECT_TRUE(p.fromFallback);
        EXPECT_FALSE(warning.isEmpty());
    }

    TEST(MachineProfileTest, ResolvePathFindsMachineJsonInConfigDir)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = writeProfile(dir, R"({"deviceId":"simulated_composite"})");
        ASSERT_FALSE(path.isEmpty());

        EXPECT_EQ(path, MachineProfileLoader::resolvePath(dir.path()));
    }

    TEST(MachineProfileTest, ResolvePathReturnsEmptyWhenNothingExists)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        EXPECT_TRUE(MachineProfileLoader::resolvePath(dir.path()).isEmpty());
    }

    /// 完整档案的各字段都要落到位，尤其是数值参数要转成字符串
    /// （HAL 侧的 ParamValue 只承载字符串，由适配器自己解析类型）。
    TEST(MachineProfileTest, ParsesFullProfile)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = writeProfile(dir, R"({
            "deviceId": "leadshine_ltdmc",
            "tickIntervalMs": 10,
            "openParams": {
                "card_no": 0,
                "axis_count": 3,
                "pulse_per_mm": 1000.0,
                "sdk_path": "C:/sdk",
                "test_mode": true
            },
            "ioPoints": [
                { "name": "safety.estop", "label": "急停", "channel": 5,
                  "direction": "input", "activeLow": true, "debounceMs": 0 },
                { "name": "out.air", "label": "吹气", "channel": 2, "direction": "output" }
            ],
            "safetyConditions": [
                { "point": "safety.estop", "description": "急停被按下",
                  "triggerOnActive": true, "severity": "emergency",
                  "actions": ["emergency", "laser_off"] }
            ]
        })");
        ASSERT_FALSE(path.isEmpty());

        MachineProfile p;
        QString error;
        ASSERT_TRUE(MachineProfileLoader::loadFromFile(path, p, error)) << error.toStdString();

        EXPECT_EQ(QStringLiteral("leadshine_ltdmc"), p.deviceId);
        EXPECT_EQ(10, p.tickIntervalMs);
        EXPECT_EQ(path, p.sourcePath);
        EXPECT_FALSE(p.fromFallback);

        EXPECT_EQ(QStringLiteral("0"), p.openParams.value(QStringLiteral("card_no")));
        EXPECT_EQ(QStringLiteral("3"), p.openParams.value(QStringLiteral("axis_count")));
        EXPECT_EQ(QStringLiteral("C:/sdk"), p.openParams.value(QStringLiteral("sdk_path")));
        // 布尔要变成 HAL 认得的 1/0，而不是 "true"
        EXPECT_EQ(QStringLiteral("1"), p.openParams.value(QStringLiteral("test_mode")));

        ASSERT_EQ(2, p.ioPoints.size());
        EXPECT_EQ(5, p.ioPoints[0].channel);
        EXPECT_TRUE(p.ioPoints[0].activeLow);
        EXPECT_FALSE(p.ioPoints[0].output);
        EXPECT_TRUE(p.ioPoints[1].output);

        ASSERT_EQ(1, p.safetyConditions.size());
        EXPECT_EQ(QStringLiteral("emergency"), p.safetyConditions[0].severity);
        EXPECT_EQ(2, p.safetyConditions[0].actions.size());
        // 没写 violateWhenInvalid 时必须是 fail-safe 的 true
        EXPECT_TRUE(p.safetyConditions[0].violateWhenInvalid);
    }

    /// JSON 语法错误必须报错，而不是静默回退 ——
    /// 「配置写错了」和「这台机器没配硬件」是两件事。
    TEST(MachineProfileTest, MalformedJsonIsReportedNotSilentlyIgnored)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = writeProfile(dir, R"({ "deviceId": )");
        ASSERT_FALSE(path.isEmpty());

        MachineProfile p;
        QString error;
        EXPECT_FALSE(MachineProfileLoader::loadFromFile(path, p, error));
        EXPECT_FALSE(error.isEmpty());
        // 失败时不得改动出参
        EXPECT_TRUE(p.deviceId.isEmpty());
    }

    TEST(MachineProfileTest, MissingDeviceIdIsRejected)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = writeProfile(dir, R"({"tickIntervalMs": 20})");
        ASSERT_FALSE(path.isEmpty());

        MachineProfile p;
        QString error;
        EXPECT_FALSE(MachineProfileLoader::loadFromFile(path, p, error));
        EXPECT_TRUE(error.contains(QStringLiteral("deviceId")));
    }

    /// 解析失败时 loadOrFallback 仍然回退（应用要能起来），但警告必须带上原因。
    TEST(MachineProfileTest, MalformedProfileStillFallsBackButWarns)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        ASSERT_FALSE(writeProfile(dir, R"(not json at all)").isEmpty());

        QString warning;
        const MachineProfile p = MachineProfileLoader::loadOrFallback(dir.path(), warning);

        EXPECT_TRUE(p.fromFallback);
        EXPECT_FALSE(warning.isEmpty());
    }

    /// 越界的 tick 周期要被纠正到默认值：1ms 的轮询会把 CPU 吃满，
    /// 10 秒的轮询等于安全联锁形同虚设。
    TEST(MachineProfileTest, OutOfRangeTickIntervalFallsBackToDefault)
    {
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        const QString path = writeProfile(
            dir, R"({"deviceId":"simulated_composite","tickIntervalMs":100000})");
        ASSERT_FALSE(path.isEmpty());

        MachineProfile p;
        QString error;
        ASSERT_TRUE(MachineProfileLoader::loadFromFile(path, p, error)) << error.toStdString();
        EXPECT_EQ(20, p.tickIntervalMs);
    }

    // ==================== DeviceHost ====================

    /// 未启动的 DeviceHost 必须拒绝开工（fail-safe），而不是「没有约束所以放行」。
    TEST(DeviceHostTest, NotStartedHostBlocksProcessing)
    {
        DeviceHost host;
        EXPECT_FALSE(host.isRunning());
        EXPECT_FALSE(host.canStartProcessing());
        EXPECT_TRUE(host.deviceId().isEmpty());
    }

    /// 急停在任何状态下都必须可调用，包括设备根本没启动的时候。
    TEST(DeviceHostTest, EmergencyStopIsSafeWhenNotStarted)
    {
        DeviceHost host;
        host.requestEmergencyStop();  // 不应崩溃
        EXPECT_FALSE(host.canStartProcessing());
    }

    TEST(DeviceHostTest, UnknownDeviceIdFailsWithReason)
    {
        MachineProfile profile;
        profile.deviceId = QStringLiteral("no_such_card_9527");

        DeviceHost host;
        QString error;
        EXPECT_FALSE(host.start(profile, error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_FALSE(host.isRunning());
    }

#ifdef ENABLE_HARDWARE

    /// 兜底档案必须真的能装配起来 —— 这是「无硬件也能完整跑通上层」的前提。
    TEST(DeviceHostTest, StartsWithBuiltinSimulatedProfile)
    {
        const MachineProfile profile = MachineProfileLoader::builtinSimulatedProfile();

        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(profile, error)) << error.toStdString();

        EXPECT_TRUE(host.isRunning());
        EXPECT_EQ(QStringLiteral("simulated_composite"), host.deviceId());
        EXPECT_FALSE(host.deviceDisplayName().isEmpty());
        // 界面上必须能区分模拟与真机，所以这个标志要如实反映
        EXPECT_TRUE(host.isSimulated());

        host.stop();
        EXPECT_FALSE(host.isRunning());
        EXPECT_TRUE(host.deviceId().isEmpty());
        // 停机后同样不允许开工
        EXPECT_FALSE(host.canStartProcessing());
    }

    /// 兜底档案装配起来之后必须**允许开工**。
    /// 「开发机能完整跑通上层」是这份档案存在的唯一意义；
    /// 若某个点位极性写反，这里会立刻抓到，而不是等到有人问「为什么开始加工是灰的」。
    TEST(DeviceHostTest, BuiltinSimulatedProfileAllowsProcessingAfterDebounce)
    {
        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(MachineProfileLoader::builtinSimulatedProfile(), error))
            << error.toStdString();

        // 第一拍之前裁决是 fail-safe 的 Blocked，必须先让安全评估跑一次
        EXPECT_FALSE(host.canStartProcessing());

        // 安全门点位 debounceMs = 20，IoPointMap::poll 要求电平稳定满该时长才采信：
        // 第一拍（t=20）只登记 pending，第二拍（t=40）才认。
        // 这里刻意分两步断言，正是为了把「一拍不够」这条时序事实钉在测试里 ——
        // 否则日后有人把它并成一拍，失败原因会被误读成极性配错。
        host.tick(20);
        EXPECT_FALSE(host.canStartProcessing());
        host.tick(20);
        EXPECT_TRUE(host.canStartProcessing()) << host.safetyViolations().join(';').toStdString();

        host.stop();
    }

    /// 重复 start 必须被拒绝：静默重入会泄漏第一台设备且让 tick 驱动错乱。

    TEST(DeviceHostTest, SecondStartIsRejected)
    {
        const MachineProfile profile = MachineProfileLoader::builtinSimulatedProfile();

        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(profile, error)) << error.toStdString();
        EXPECT_FALSE(host.start(profile, error));
        EXPECT_FALSE(error.isEmpty());

        host.stop();
    }

    /// stop() 必须幂等：退出路径上会被组合根析构与 shutdown 各调一次。
    TEST(DeviceHostTest, StopIsIdempotent)
    {
        const MachineProfile profile = MachineProfileLoader::builtinSimulatedProfile();

        DeviceHost host;
        QString error;
        ASSERT_TRUE(host.start(profile, error)) << error.toStdString();
        host.stop();
        host.stop();  // 不应崩溃
        EXPECT_FALSE(host.isRunning());
    }

    /// autoOpen=false 是现场排查接线用的模式：设备被创建但不打开，
    /// 这不算失败，但也绝不能被当成「已就绪」。
    TEST(DeviceHostTest, AutoOpenFalseCreatesDeviceWithoutOpening)
    {
        MachineProfile profile = MachineProfileLoader::builtinSimulatedProfile();
        profile.autoOpen = false;

        DeviceHost host;
        QString error;
        EXPECT_TRUE(host.start(profile, error)) << error.toStdString();
        // 没有 open，就没有 tick 驱动，因此 isRunning 为 false
        EXPECT_FALSE(host.isRunning());
        EXPECT_FALSE(host.canStartProcessing());

        host.stop();
    }

    /// 档案配了 IO 点位但设备没有 IO 能力时必须在启动阶段就失败。
    /// 否则表现是「所有安全条件都 invalid → 永远无法开工」，
    /// 而用户看到的只有一句「无法开工」，找不到原因。
    TEST(DeviceHostTest, IoPointsOnDeviceWithoutIoCapabilityFailFast)
    {
        MachineProfile profile;
        // ruida_rdc 只提供 IMotionCard + ILaserSource，没有 IIoModule
        profile.deviceId = QStringLiteral("ruida_rdc");
        profile.openParams.insert(QStringLiteral("transport"), QStringLiteral("file"));
        profile.openParams.insert(QStringLiteral("output_path"),
            QDir::temp().filePath(QStringLiteral("hw_assembly_test.rd")));

        MachineIoPointConfig pt;
        pt.name = QStringLiteral("safety.estop");
        pt.channel = 0;
        profile.ioPoints.append(pt);

        DeviceHost host;
        QString error;
        EXPECT_FALSE(host.start(profile, error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_FALSE(host.isRunning());
    }

#else

    /// 关掉 BUILD_HARDWARE 时必须明确失败，不能假装启动成功 ——
    /// 否则上层以为设备就绪，第一条加工命令才炸，且错误与真实原因无关。
    TEST(DeviceHostTest, StartFailsClearlyWhenHardwareIsNotCompiledIn)
    {
        const MachineProfile profile = MachineProfileLoader::builtinSimulatedProfile();

        DeviceHost host;
        QString error;
        EXPECT_FALSE(host.start(profile, error));
        EXPECT_FALSE(error.isEmpty());
        EXPECT_FALSE(DeviceHost::isHardwareSupportCompiled());
    }

#endif
}
