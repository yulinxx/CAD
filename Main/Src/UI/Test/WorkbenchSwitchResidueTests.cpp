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
 *   6. 切换完成后状态栏消息区回到 "Ready"，不停在切换提示
 *   7. 2D 相机（缩放 + 平移）跨视口重建存活
 *      （RenderViewport2D 每次 attach 新建，不抄状态就重置回默认台面视图）
 *   8. 切换不累积工具栏（配置驱动工具栏需先清后建）
 *
 * 环境说明
 * --------
 * MainTests 是 gtest 控制台程序（GTest::gtest_main），本身不带 QApplication。
 * 需要 QWidget 的用例自己兜一个：只在 qApp 为空时创建，并持有到进程结束。
 */

#include <gtest/gtest.h>

#include <QApplication>
#include <QString>
#include <QToolBar>
#include <QVariantMap>

#include <memory>
#include <utility>

#include "UI/Services/UiStateCenter.h"
#include "UI/StatusBarBase.h"
#include "UI/StatusIndicators.h"
#include "UI/Workbench/UiWorkbench.h"
#include "UI/Workbench/WorkbenchWindow.h"

#include "Composition/OpRegistryTypes.h"
#include "UI2D/Operation/OperationBus.h"

#include "RenderViewport2D.h"
#include "UI/Render/Camera2D.h"

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
        explicit ProbeStatusBar(QWidget* parent = nullptr)
            : StatusBarBase(parent)
        {
            setupStandardIndicators(200, 120, 200, /*infoBeforeMessage=*/true);
        }
    };

    /// 假工作台：只提供身份与空实现，用来驱动 WorkbenchWindow 的切换流程。
    /// 不用真工作台是为了避开 OpenGL 视口 —— 测试环境没有 GL 上下文。
    /// 可选的状态栏用来验证「切换完成后状态栏消息区显示什么」，
    /// 真实工作台同样是在 attachToWindow 里 mountStatusBar 的。
    class FakeWorkbench : public UiWorkbench
    {
    public:
        explicit FakeWorkbench(QString workbenchId, StatusBarBase* statusBar = nullptr)
            : m_id(std::move(workbenchId))
            , m_statusBar(statusBar)
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

        void attachToWindow(WorkbenchWindow& window) override
        {
            if (m_statusBar)
            {
                window.mountStatusBar(m_statusBar);
            }
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
        StatusBarBase* m_statusBar{ nullptr };
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

    UiStateCenter stateCenter;
    WorkbenchServices services;
    services.uiState.stateCenter = &stateCenter;
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

// ==================== 切换完成后状态栏不得停在切换提示上 ====================

TEST(WorkbenchSwitchResidueTest, MountedStatusBarShowsReadyAfterSwitch)
{
    ensureApplication();

    UiStateCenter stateCenter;
    WorkbenchWindow window;
    window.setUiStateCenter(&stateCenter);

    auto* bar2D = new ProbeStatusBar(&window);
    auto* bar3D = new ProbeStatusBar(&window);

    FakeWorkbench workbench2D(QStringLiteral("2D"), bar2D);
    FakeWorkbench workbench3D(QStringLiteral("3D"), bar3D);

    window.setWorkbench(&workbench2D);
    window.setWorkbenchFactory([&](const QString& id) -> UiWorkbench* {
        return (id == QStringLiteral("3D")) ? static_cast<UiWorkbench*>(&workbench3D)
                                            : static_cast<UiWorkbench*>(&workbench2D);
    });
    workbench2D.attachToWindow(window);
    workbench2D.activate();

    window.triggerWorkbench(QStringLiteral("3D"));

    // 新工作台接管状态栏后，消息区应由 refreshFromState 写回 "Ready"。
    // 之前的 bug 是提示清不掉，这一栏会永久停在 "Switching to 3D"。
    ASSERT_EQ(window.activeStatusBar(), bar3D);
    const QString text = bar3D->messageIndicatorText();
    EXPECT_FALSE(text.contains(QStringLiteral("Switching"))) << text.toStdString();
    EXPECT_FALSE(text.isEmpty()) << text.toStdString();
}

// ==================== 2D 相机跨视口重建存活 ====================

TEST(WorkbenchSwitchResidueTest, TwoDimensionalCameraStateSurvivesViewportRecreation)
{
    ensureApplication();

    // 模拟切走前：用户在 2D 里缩放 + 平移
    RenderViewport2D first;
    Camera2D camera = first.cameraState();
    camera.zoomX = 3.5f;
    camera.zoomY = 2.5f;
    camera.panOffset = QPointF(-123.0, -456.0);
    first.setCameraState(camera);

    // Workbench2D::deactivate() 在视口销毁前抄出状态
    const Camera2D saved = first.cameraState();

    // 切回来：RenderViewport2D 是新建的，构造函数的相机是硬编码默认台面视图
    RenderViewport2D second;
    EXPECT_NE(second.cameraState().panOffset, saved.panOffset);

    // Workbench2D::attachToWindow() 恢复
    second.setCameraState(saved);

    const Camera2D restored = second.cameraState();
    EXPECT_FLOAT_EQ(restored.zoomX, 3.5f);
    EXPECT_FLOAT_EQ(restored.zoomY, 2.5f);
    EXPECT_DOUBLE_EQ(restored.panOffset.x(), -123.0);
    EXPECT_DOUBLE_EQ(restored.panOffset.y(), -456.0);
}

// ==================== 脏标记：dirty(任一) 与 activeDocDirty(当前台) 分离 ====================

TEST(WorkbenchSwitchResidueTest, DirtyProbeSeparatesAnyDirtyFromActiveWorkbenchDirty)
{
    ensureApplication();

    UiStateCenter stateCenter;
    bool dirty2D = false;
    bool dirty3D = false;
    stateCenter.registerDirtyProbe(QStringLiteral("2D"), [&] { return dirty2D; });
    stateCenter.registerDirtyProbe(QStringLiteral("3D"), [&] { return dirty3D; });
    stateCenter.setCurrentWorkbenchId(QStringLiteral("2D"));

    // 两台都干净
    stateCenter.refreshDirty();
    EXPECT_FALSE(stateCenter.dirty());
    EXPECT_FALSE(stateCenter.activeDocDirty());

    // 只有 3D 脏：关闭提示要看到它（dirty=任一），但 2D 标题不能挂 *
    dirty3D = true;
    stateCenter.refreshDirty();
    EXPECT_TRUE(stateCenter.dirty());
    EXPECT_FALSE(stateCenter.activeDocDirty());

    // 当前台也脏：标题才带 *
    dirty2D = true;
    stateCenter.refreshDirty();
    EXPECT_TRUE(stateCenter.dirty());
    EXPECT_TRUE(stateCenter.activeDocDirty());

    // 当前台存盘、另一台仍脏：* 消失，关闭提示保留
    dirty2D = false;
    stateCenter.refreshDirty();
    EXPECT_TRUE(stateCenter.dirty());
    EXPECT_FALSE(stateCenter.activeDocDirty());
}

TEST(WorkbenchSwitchResidueTest, ActiveDocDirtyFollowsCurrentWorkbench)
{
    ensureApplication();

    UiStateCenter stateCenter;
    bool dirty2D = false;
    bool dirty3D = false;
    stateCenter.registerDirtyProbe(QStringLiteral("2D"), [&] { return dirty2D; });
    stateCenter.registerDirtyProbe(QStringLiteral("3D"), [&] { return dirty3D; });

    dirty3D = true;
    stateCenter.setCurrentWorkbenchId(QStringLiteral("2D"));
    EXPECT_FALSE(stateCenter.activeDocDirty());
    EXPECT_TRUE(stateCenter.dirty());

    // 切到 3D：标题 * 跟随目标工作台（setCurrentWorkbenchId 内部会重算）
    stateCenter.setCurrentWorkbenchId(QStringLiteral("3D"));
    EXPECT_TRUE(stateCenter.activeDocDirty());
    EXPECT_TRUE(stateCenter.dirty());
}

TEST(WorkbenchSwitchResidueTest, RegisterDirtyProbeAbsorbsExistingDirtyState)
{
    ensureApplication();

    UiStateCenter stateCenter;
    bool dirty = true;  // 注册前文档就已经脏了
    stateCenter.registerDirtyProbe(QStringLiteral("2D"), [&] { return dirty; });
    stateCenter.setCurrentWorkbenchId(QStringLiteral("2D"));

    // 注册即吸收：不能要求调用方记得再 refreshDirty 一次
    EXPECT_TRUE(stateCenter.dirty());
    EXPECT_TRUE(stateCenter.activeDocDirty());
}

TEST(WorkbenchSwitchResidueTest, ActiveWorkbenchWithoutProbeFallsBackToAnyDirty)
{
    ensureApplication();

    UiStateCenter stateCenter;
    bool dirty3D = true;
    stateCenter.registerDirtyProbe(QStringLiteral("3D"), [&] { return dirty3D; });
    // 当前工作台（2D）还没注册探针（2D 尚未初始化）：
    // 退回「任一脏」—— 宁可标题多带 * 也不漏掉未保存提示
    stateCenter.setCurrentWorkbenchId(QStringLiteral("2D"));

    EXPECT_TRUE(stateCenter.dirty());
    EXPECT_TRUE(stateCenter.activeDocDirty());
}

TEST(WorkbenchSwitchResidueTest, DirtyWorkbenchIdsListsOnlyDirtyWorkbenches)
{
    ensureApplication();

    UiStateCenter stateCenter;
    bool dirty2D = false;
    bool dirty3D = false;
    stateCenter.registerDirtyProbe(QStringLiteral("2D"), [&] { return dirty2D; });
    stateCenter.registerDirtyProbe(QStringLiteral("3D"), [&] { return dirty3D; });
    stateCenter.setCurrentWorkbenchId(QStringLiteral("2D"));

    // 关闭流程按这份列表逐台保存（closeEvent 只保存活跃台会丢另一台的未存更改）
    EXPECT_TRUE(stateCenter.dirtyWorkbenchIds().isEmpty());

    dirty3D = true;
    EXPECT_EQ(stateCenter.dirtyWorkbenchIds(), QStringList{ QStringLiteral("3D") });

    dirty2D = true;
    const QStringList both = stateCenter.dirtyWorkbenchIds();
    EXPECT_EQ(both.size(), 2);
    EXPECT_TRUE(both.contains(QStringLiteral("2D")));
    EXPECT_TRUE(both.contains(QStringLiteral("3D")));

    dirty3D = false;
    EXPECT_EQ(stateCenter.dirtyWorkbenchIds(), QStringList{ QStringLiteral("2D") });
}

// ==================== 保存命令必须返回真实结果（File_Save 恒 success=true 的回归防护） ====================

TEST(WorkbenchSwitchResidueTest, VoidLambdaAlwaysReportsSuccess_documentingWhyFileSaveNeededDeclareWithResult)
{
    ensureApplication();

    // 对照组（旧行为）：void 闭包经 Ops::declare 包装后 execute 无条件 success=true ——
    // 这正是 closeEvent / 新建流程曾经永远走不到"保存失败或取消"分支的根源
    OperationBus bus;
    Ops::declare(bus.registry(), OperationId::File_New, [] {});
    const OperationResult result = bus.run(OperationId::File_New, QVariantMap{});
    EXPECT_TRUE(result.success);
}

TEST(WorkbenchSwitchResidueTest, ResultOperationPropagatesCancelThroughBus)
{
    ensureApplication();

    // declareWithResult 必须把取消标记原样透传：closeEvent 据此中止关闭而不是弹"保存失败，重试？"
    OperationBus bus;
    Ops::declareWithResult(bus.registry(), OperationId::File_Save, [] {
        OperationResult r;
        r.message = QStringLiteral("Save was cancelled.");
        r.flags |= Cmd::OpFlagCanceled;
        return r;
    });

    const OperationResult result = bus.run(OperationId::File_Save, QVariantMap{});
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.canceled());
    EXPECT_EQ(result.message, QStringLiteral("Save was cancelled."));
}

TEST(WorkbenchSwitchResidueTest, ResultOperationPropagatesPlainFailureThroughBus)
{
    ensureApplication();

    OperationBus bus;
    Ops::declareWithResult(bus.registry(), OperationId::File_SaveAs, [] {
        OperationResult r;
        r.message = QStringLiteral("disk full");
        return r;
    });

    const OperationResult result = bus.run(OperationId::File_SaveAs, QVariantMap{});
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.canceled());
    EXPECT_EQ(result.message, QStringLiteral("disk full"));
}

// ==================== 切换不得累积工具栏 ====================

TEST(WorkbenchSwitchResidueTest, SwitchingWorkbenchDoesNotAccumulateToolBars)
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
    const int afterFirstSwitch = window.findChildren<QToolBar*>().size();

    window.triggerWorkbench(QStringLiteral("2D"));
    const int afterSecondSwitch = window.findChildren<QToolBar*>().size();

    // 6b.1 新增的 buildToolBars() 走的是 clearLayoutContent 重建路径，
    // 每个切换周期都必须先清后建；只建不清会让工具栏一轮轮叠加。
    EXPECT_EQ(afterSecondSwitch, afterFirstSwitch);
}
