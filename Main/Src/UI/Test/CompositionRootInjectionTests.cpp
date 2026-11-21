/**
 * @file CompositionRootInjectionTests.cpp
 * @brief 组合根静态入口清理的回归测试：
 *  1. build() 幂等：重复调用不重复接线、仍返回 true
 *  2. 装配完成前 instance() 恒为空，不暴露半初始化对象；装配后暴露、析构后回收
 *  3. 设备面板从所属 WorkbenchWindow 拉取依赖，而非回查组合根单例
 *
 * 环境说明：MainTests 是 gtest 控制台程序（GTest::gtest_main），默认没有
 * QApplication；需要 QWidget 的用例自己兜一个（offscreen 平台，避免弹窗）。
 */

#include <gtest/gtest.h>

#include <QApplication>

#include <memory>

#include "Composition/ApplicationCompositionRoot.h"
#include "Hardware/DeviceHost.h"
#include "Hardware/ProcessingJobService.h"
#include "UI/Safety/EmergencyStopController.h"
#include "UI/Widgets/DevicePanel.h"
#include "UI/Workbench/WorkbenchWindow.h"
#include "UI2D/Operation/OperationBus.h"

namespace
{
/// 确保进程内有且仅有一个 QApplication（offscreen，避免测试弹窗）。
void ensureApplication()
{
    if (qApp)
    {
        return;
    }
    static int argc = 1;
    static char appName[] = "MainTests";
    static char* argv[] = { appName, nullptr };
    static QApplication app(argc, argv);
}
}  // namespace

// 2. 装配前 instance() 为空：构造但不 build() 时不得暴露半初始化对象。
//    本用例不依赖 build() 成功，是最基础的契约守护，须在本文件最先运行。
TEST(CompositionRootLifecycle, InstanceIsNullBeforeAssembly)
{
    ensureApplication();

    {
        ApplicationCompositionRoot root;
        EXPECT_FALSE(root.isBuilt());
        // 未成功装配前，instance() 必须为空
        EXPECT_EQ(ApplicationCompositionRoot::instance(), nullptr);
    }

    // 未装配的根析构后，instance() 仍应为空
    EXPECT_EQ(ApplicationCompositionRoot::instance(), nullptr);
}

// 1. build() 幂等 + 2. 装配后 instance 暴露、析构后回收。
TEST(CompositionRootLifecycle, BuildIsIdempotentAndInstanceLifecycle)
{
    ensureApplication();

    auto root = std::make_unique<ApplicationCompositionRoot>();
    ASSERT_FALSE(root->isBuilt());
    ASSERT_EQ(ApplicationCompositionRoot::instance(), nullptr);

    if (!root->build())
    {
        // 测试环境无法完成完整装配（如缺少数据库/配置目录）：跳过幂等校验，
        // 但失败可见契约仍须成立——装配失败时 instance() 保持为空。
        EXPECT_EQ(ApplicationCompositionRoot::instance(), nullptr);
        GTEST_SKIP() << "build() did not complete in test harness; skipping idempotency/lifecycle assertions";
    }

    EXPECT_TRUE(root->isBuilt());
    ASSERT_NE(ApplicationCompositionRoot::instance(), nullptr);
    EXPECT_EQ(ApplicationCompositionRoot::instance(), root.get());

    // 幂等：第二次 build() 直接返回 true，不重复接线、不改变 instance
    EXPECT_TRUE(root->build()) << "duplicate build() must be idempotent and return true";
    EXPECT_TRUE(root->isBuilt());
    EXPECT_EQ(ApplicationCompositionRoot::instance(), root.get());

    // 析构回收 instance()，防止跨用例串台
    root.reset();
    EXPECT_EQ(ApplicationCompositionRoot::instance(), nullptr);
}

// 3. 设备面板从所属窗口取依赖：窗口注入服务后，默认构造的子面板能绑定成功；
//    无父窗口的面板则保持未绑定（不会回查组合根单例）。
TEST(CompositionRootInjection, DevicePanelPullsDependenciesFromOwningWindow)
{
    ensureApplication();

    DeviceHost deviceHost;
    // 仅需对象存在以验证指针透传，不真正启动硬件
    auto jobService = std::make_unique<ProcessingJobService>(&deviceHost);
    auto safety = std::make_unique<EmergencyStopController>();
    OperationBus bus;

    WorkbenchWindow window;
    window.bindDeviceServices(&deviceHost, jobService.get());
    window.bindSafetyServices(safety.get(), &bus);

    // 窗口侧注入契约：访问器返回注入的指针
    EXPECT_EQ(window.deviceHost(), &deviceHost);
    EXPECT_EQ(window.processingJobService(), jobService.get());
    EXPECT_EQ(window.emergencyStopController(), safety.get());

    // 面板默认构造 + 父对象为窗口 → 应从窗口拉到依赖并标记已绑定
    auto* panel = new DevicePanel(&window);
    EXPECT_TRUE(panel->isBound()) << "parented to a configured window must bind services";
    // panel 由 window 持有，window 析构时释放，无需手动 delete

    // 无父窗口的面板 → 找不到 WorkbenchWindow，保持未绑定（不回查组合根）。
    // 默认构造函数已调用一次 bindServicesIfNeeded()，此处无需再调。
    DevicePanel orphan;
    EXPECT_FALSE(orphan.isBound()) << "no owning WorkbenchWindow must leave the panel unbound";
}
