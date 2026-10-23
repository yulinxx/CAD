/**
 * @file WorkbenchSwitchResidueTests.cpp
 * @brief 2D/3D 工作台切换回归测试 —— 残留（residue）与丢失（loss）两类
 *
 * 背景
 * ----
 * 2D ↔ 3D 切换走 WorkbenchWindow::triggerWorkbench：
 *   deactivate → 清 transient 状态 → 拆 UI（工具栏 / Dock / 中央控件）→ 重建 → activate。
 * 框架层的「拆了重建」做得比较干净，但有几处状态会跨工作台串台，
 * 本文件把那些行为固化成回归断言：
 *
 *   1. 3D 状态栏忽略窗口级的二维坐标推送
 *      （切到 3D 后，位置栏不能显示上一个 2D 工作台留下的坐标）
 *   2. 2D 状态栏照常接受二维坐标推送
 *      （不能为了修 1 把 2D 也堵死）
 *   3. 卸载状态栏后窗口不再持有它，且旧状态栏被显式隐藏
 *   4. 切换完成后状态中心不残留切换期的提示文案
 *      （状态栏消息区显示的就是它 → 切到 3D 后那栏一直停在「切换到 3D」）
 *   5. 3D 撤销栈跨 deactivate 存活
 *      （切走再切回来仍能撤销；否则 3D 里做的修改再也 Ctrl+Z 不回去）
 *
 * 环境说明
 * --------
 * MainTests 是 gtest 控制台程序（GTest::gtest_main），本身不带 QApplication。
 * 需要 QWidget 的用例自己兜一个：只在 qApp 为空时创建，并持有到进程结束。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <utility>

#include "UI/Services/UiStateCenter.h"
#include "UI/StatusBarBase.h"
#include "UI/StatusIndicators.h"
#include "UI/Workbench/UiWorkbench.h"
#include "UI/Workbench/WorkbenchWindow.h"

#if BUILD_UI3D
    #include "Engine/Edit/IUndoRedoCommand.h"
    #include "Engine3D/SceneManager3D.h"
    #include "Import/ImportService.h"
    #include "UI/StatusBar/StatusBar3D.h"
    #include "UI/Workbench/Workbench3D.h"
    #include "UI3D/Edit/UndoRedoManager3D.h"
#endif

namespace
{
    /// MainTests 是 gtest 控制台程序（GTest::gtest_main），默认没有 QApplication。
    /// 需要 QWidget 的用例自己兜一个：只在 qApp 为空时创建，且持有到进程结束。
    void ensureApplication()
    {
        static std::unique_ptr<QApplication> app;
        if (!qApp)
        {
            static int argc = 1;
            static char appName[] = "MainTests";
            static char* argv[] = { appName, nullptr };
            app = std::make_unique<QApplication>(argc, argv);
        }
    }

    /// 探针状态栏：走基类的二维坐标路径（与 2D 状态栏同一条实现）。
    class ProbeStatusBar : public StatusBarBase
    {
    public:
        ProbeStatusBar()
        {
            setupStandardIndicators(200, 120, 200, /*infoBeforeMessage=*/true);
        }
    };

    /// 假工作台：只提供身份与空实现，用来驱动 WorkbenchWindow 的切换流程。
    /// 不用真工作台是为了避开 OpenGL 视口 —— 测试环境没有 GL 上下文。
    class FakeWorkbench : public UiWorkbench
    {
    public:
        explicit FakeWorkbench(QString workbenchId)
            : m_id(std::move(workbenchId))
        {
        }

        QString id() const override
        {
            return m_id;
        }

        QString displayName() const override
        {
            return m_id;
        }

        bool initialize(const WorkbenchServices&) override
        {
            return true;
        }

        void attachToWindow(WorkbenchWindow&) override
        {
        }

        void activate() override
        {
        }

        void deactivate() override
        {
        }

        void shutdown() override
        {
        }

        void dispatchCommand(const QString&, const QVariantMap&) override
        {
        }

        QString commandText(const QString&) const override
        {
            return QString();
        }

    private:
        QString m_id;
    };

#if BUILD_UI3D
    /// 假命令：只用于验证撤销栈里是否还留着内容。
    class FakeUndoCommand : public IUndoRedoCommand
    {
    public:
        void execute() override
        {
            ++executed;
        }

        void undo() override
        {
            ++undone;
        }

        const char* description() const override
        {
            return "Fake";
        }

        int executed{ 0 };
        int undone{ 0 };
    };
#endif
}  // namespace

// ==================== 状态栏坐标：2D / 3D 隔离 ====================

#if BUILD_UI3D
TEST(WorkbenchSwitchResidueTest, ThreeDimensionalStatusBarIgnoresTwoDimensionalPositionPush)
{
    ensureApplication();

    StatusBar3D bar;
    bar.setCursorPosition(1.0, 2.0, 3.0, true);

    const QString threeDimensional = bar.positionIndicatorText();
    ASSERT_FALSE(threeDimensional.isEmpty());
    EXPECT_TRUE(threeDimensional.contains(QStringLiteral("1.00"))) << threeDimensional.toStdString();
    EXPECT_TRUE(threeDimensional.contains(QStringLiteral("3.00"))) << threeDimensional.toStdString();

    // 窗口级 refreshPositionLabel 推的是二维语义（单位切换 / 语言切换都会走到它）：
    // 它绝不能把三维位置栏覆盖成二维带单位坐标 —— 那正是「切到 3D 后状态栏
    // 还显示 2D 坐标」的来源（2D 的最后一次鼠标位置留在窗口级状态里）。
    bar.setPositionValues(100.0, 200.0, QStringLiteral("mm"), true);
    EXPECT_EQ(bar.positionIndicatorText(), threeDimensional);
}
#endif

TEST(WorkbenchSwitchResidueTest, TwoDimensionalStatusBarStillAppliesTwoDimensionalPositionPush)
{
    ensureApplication();

    ProbeStatusBar bar;
    bar.setPositionValues(12.5, 34.5, QStringLiteral("mm"), true);

    const QString text = bar.positionIndicatorText();
    EXPECT_TRUE(text.contains(QStringLiteral("12.50"))) << text.toStdString();
    EXPECT_TRUE(text.contains(QStringLiteral("34.50"))) << text.toStdString();
}

#if BUILD_UI3D
TEST(WorkbenchSwitchResidueTest, WindowLevelPositionLabelDoesNotLeakIntoThreeDimensionalStatusBar)
{
    ensureApplication();

    WorkbenchWindow window;
    auto* bar = new StatusBar3D(&window);
    window.mountStatusBar(bar);
    ASSERT_EQ(window.activeStatusBar(), bar);

    // 模拟 2D 工作台留下的鼠标位置（窗口级状态，跨工作台共享）
    window.updatePositionLabel(123.0, 456.0);

    const QString text = bar->positionIndicatorText();
    EXPECT_FALSE(text.contains(QStringLiteral("123.00"))) << text.toStdString();
    EXPECT_FALSE(text.contains(QStringLiteral("456.00"))) << text.toStdString();
}
#endif

// ==================== 状态栏挂载 / 卸载 ====================

TEST(WorkbenchSwitchResidueTest, UnmountStatusBarClearsMountedBarAndHidesIt)
{
    ensureApplication();

    WorkbenchWindow window;
    auto* bar = new ProbeStatusBar(&window);
    window.mountStatusBar(bar);
    ASSERT_EQ(window.activeStatusBar(), bar);

    window.unmountStatusBar();

    // 窗口不再持有旧状态栏
    EXPECT_EQ(window.activeStatusBar(), nullptr);
    // 且旧状态栏必须被显式隐藏：removeWidget 只是取消 QStatusBar 的托管，
    // 控件仍然活着 —— 不显式隐藏就可能留在 3D 状态栏旁边显示（2D 残留）。
    EXPECT_TRUE(bar->isHidden());
}

// ==================== 切换期的临时提示不得残留 ====================

TEST(WorkbenchSwitchResidueTest, SwitchDoesNotLeaveTransientPromptInStateCenter)
{
    ensureApplication();

    UiStateCenter stateCenter;
    WorkbenchWindow window;
    window.setUiStateCenter(&stateCenter);

    FakeWorkbench workbench2D(QStringLiteral("2D"));
    FakeWorkbench workbench3D(QStringLiteral("3D"));

    window.setWorkbench(&workbench2D);
    window.setWorkbenchFactory([&](const QString& id) -> UiWorkbench* {
        return (id == QStringLiteral("3D")) ? static_cast<UiWorkbench*>(&workbench3D)
                                            : static_cast<UiWorkbench*>(&workbench2D);
    });
    workbench2D.attachToWindow(window);
    workbench2D.activate();

    window.triggerWorkbench(QStringLiteral("3D"));

    // 切换期会把「Switching to 3D」写进 statusPrompt，而状态栏消息区显示的就是它。
    // 3D 侧没有任何后续推送（3D 视口不回调 setStatusCallback），
    // 不清掉的话切到 3D 后状态栏中间那栏会一直停在切换提示上。
    EXPECT_TRUE(stateCenter.snapshot().statusPrompt.isEmpty())
        << "statusPrompt=" << stateCenter.snapshot().statusPrompt.toStdString();
}

// ==================== 3D 撤销栈跨工作台切换存活 ====================

#if BUILD_UI3D
TEST(WorkbenchSwitchResidueTest, ThreeDimensionalUndoStackSurvivesWorkbenchDeactivate)
{
    ensureApplication();

    Eg::SceneManager3D scene3D;
    ImportService importService;
    importService.setSceneManager3D(&scene3D);

    WorkbenchServices services;
    services.persistence.importService = &importService;

    Workbench3D workbench;
    ASSERT_TRUE(workbench.initialize(services));

    UndoRedoManager3D* undo = workbench.undoRedoManager3D();
    ASSERT_NE(undo, nullptr);
    EXPECT_FALSE(undo->canUndo());

    undo->pushCommand(new FakeUndoCommand());
    EXPECT_TRUE(undo->canUndo());

    // 模拟工作台切换：deactivate 会销毁 ServiceOwner（渲染链、视口、场景编辑服务……），
    // 但撤销栈必须留着 —— 否则切到 2D 再切回来，3D 里做的修改就再也撤销不了。
    workbench.deactivate();

    UndoRedoManager3D* afterDeactivate = workbench.undoRedoManager3D();
    ASSERT_NE(afterDeactivate, nullptr);
    // 同一个实例（没有被重建）且内容还在
    EXPECT_EQ(afterDeactivate, undo);
    EXPECT_TRUE(afterDeactivate->canUndo());
}
#endif
