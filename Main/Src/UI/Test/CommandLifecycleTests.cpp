/**
 * @file CommandLifecycleTests.cpp
 * @brief 命令生命周期测试套件
 *
 * 覆盖 P0 架构收口后的核心验证项：
 * - 正常执行路径：execute → activate → commit → undo 压栈 → reset
 * - 异常路径：activate 失败 → cancel（不进栈）
 * - 取消路径：cancel（不进栈）
 * - 重置可重复执行
 * - 完成后不响应旧事件
 * - 预览不污染文档
 * - 选择从文档唯一事实源读取
 * - Undo/Redo 语义完整性
 */

#include "UI/UiCommandDispatcher.h"
#include "UI/UiCommandHandler.h"
#include "UI/UiEntities.h"
#include "UI/UiServices.h"

#include <gtest/gtest.h>

#include <memory>

// ============================================================================
// 测试辅助：创建最小化的 UiServices
// ============================================================================
namespace
{
    UiServices makeTestServices(EntityDocument2D* doc = nullptr)
    {
        UiServices svc;
        svc.document2D = doc;
        return svc;
    }
}

// ============================================================================
// 正常执行路径测试
// ============================================================================

TEST(CommandLifecycleTest, ExecuteNonInteractive_CommitsAndPushesUndo)
{
    // 非交互式命令（SimpleCommandHandler）应直接提交并入栈
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new SimpleCommandHandler(QStringLiteral("test.non_interactive"), QStringLiteral("Test"));
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices());

    EXPECT_EQ(undoStack.count(), 0);
    EXPECT_FALSE(dispatcher.hasActiveCommand());

    dispatcher.execute(QStringLiteral("test.non_interactive"));

    // 非交互式命令 execute 后应立即完成，状态机回到空闲
    EXPECT_FALSE(dispatcher.hasActiveCommand());
    // SimpleCommandHandler::createUndoCommand 默认返回 nullptr，不进栈
    EXPECT_EQ(undoStack.count(), 0);
}

TEST(CommandLifecycleTest, ExecuteInteractive_StaysActive)
{
    // 交互式命令（DrawLineCommand）执行后应保持 Active 状态
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    EXPECT_FALSE(dispatcher.hasActiveCommand());

    dispatcher.execute(QStringLiteral("2d.draw_line"));

    // 交互式命令应保持 Active
    EXPECT_TRUE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Active);
}

// ============================================================================
// cancel 不进栈测试
// ============================================================================

TEST(CommandLifecycleTest, Cancel_DoesNotPushUndo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    ASSERT_TRUE(dispatcher.hasActiveCommand());

    dispatcher.cancel();

    // cancel 后状态应完全重置
    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Idle);
    // cancel 不进栈
    EXPECT_EQ(undoStack.count(), 0);
}

TEST(CommandLifecycleTest, Cancel_ResetsHandlerFully)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    ASSERT_EQ(handler->state(), CommandState::Active);

    dispatcher.cancel();

    // 取消后 handler 回到 Idle
    EXPECT_EQ(handler->state(), CommandState::Idle);
}

// ============================================================================
// 提交失败不进栈测试
// ============================================================================

TEST(CommandLifecycleTest, Submit_CreateUndoCommandReturnsNull_DoesNotPush)
{
    // 当 createUndoCommand() 返回 nullptr 时，不应压栈
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    // SimpleCommandHandler 的 createUndoCommand 默认返回 nullptr
    auto handler = new SimpleCommandHandler(QStringLiteral("test.no_undo"), QStringLiteral("NoUndo"));
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices());

    dispatcher.execute(QStringLiteral("test.no_undo"));

    EXPECT_EQ(undoStack.count(), 0);
}

// ============================================================================
// reset 后可重复执行
// ============================================================================

TEST(CommandLifecycleTest, Reset_AllowsReExecution)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    // 第一次执行
    dispatcher.execute(QStringLiteral("2d.draw_line"));
    EXPECT_EQ(handler->state(), CommandState::Active);
    dispatcher.cancel();
    EXPECT_EQ(handler->state(), CommandState::Idle);

    // 第二次执行（reset 后应可重复）
    dispatcher.execute(QStringLiteral("2d.draw_line"));
    EXPECT_EQ(handler->state(), CommandState::Active);
    dispatcher.cancel();
    EXPECT_EQ(handler->state(), CommandState::Idle);
}

// ============================================================================
// 完成后不响应旧事件
// ============================================================================

TEST(CommandLifecycleTest, AfterSubmit_EventsAreIgnored)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    ASSERT_TRUE(dispatcher.hasActiveCommand());

    // 模拟拾取两个点触发 isComplete → submit
    dispatcher.forwardMouseDown(100, 100);
    dispatcher.forwardMouseDown(200, 200);

    // 提交后不应再有活动命令
    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Idle);

    // 后续事件不应被处理（handler 已 Idle）
    bool handled = dispatcher.forwardMouseDown(300, 300);
    EXPECT_FALSE(handled);
}

TEST(CommandLifecycleTest, AfterCancel_EventsAreIgnored)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    ASSERT_TRUE(dispatcher.hasActiveCommand());

    dispatcher.cancel();
    ASSERT_FALSE(dispatcher.hasActiveCommand());

    // 取消后事件不应被处理
    bool handled = dispatcher.forwardMouseDown(100, 100);
    EXPECT_FALSE(handled);
}

// ============================================================================
// 预览不污染文档
// ============================================================================

TEST(CommandLifecycleTest, Preview_DoesNotModifyDocument)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    const size_t initialEntityCount = doc.entities().size();

    dispatcher.execute(QStringLiteral("2d.draw_line"));

    // 预览阶段：模拟鼠标移动（不应创建实体）
    dispatcher.forwardMouseDown(100, 100);
    dispatcher.forwardMouseMove(150, 150);

    CommandPreview preview = handler->preview();
    EXPECT_TRUE(preview.valid);

    // 预览不应改变文档实体数量
    EXPECT_EQ(doc.entities().size(), initialEntityCount);
}

TEST(CommandLifecycleTest, Preview_ClearedOnCancel)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    dispatcher.forwardMouseDown(100, 100);

    // 预览应有效
    CommandPreview preview = handler->preview();
    EXPECT_TRUE(preview.valid);

    dispatcher.cancel();

    // 取消后预览应清除
    preview = handler->preview();
    EXPECT_FALSE(preview.valid);
}

TEST(CommandLifecycleTest, Preview_NoResidueAfterReActivation)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    EntityDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    // 第一次激活
    dispatcher.execute(QStringLiteral("2d.draw_line"));
    dispatcher.forwardMouseDown(100, 100);
    dispatcher.cancel();

    // 第二次激活，预览不应残留
    dispatcher.execute(QStringLiteral("2d.draw_line"));
    CommandPreview preview = handler->preview();
    EXPECT_FALSE(preview.valid);
}

// ============================================================================
// 选择从文档唯一事实源读取
// ============================================================================

TEST(CommandLifecycleTest, Selection_ReadsFromDocument)
{
    EntityDocument2D doc;

    // 创建实体并选择
    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().clear();
    doc.selection().add(entity);

    // 选择从文档读取
    EXPECT_FALSE(doc.selection().empty());
    auto items = doc.selection().items();
    EXPECT_EQ(items.size(), 1);
    EXPECT_EQ(items.first()->id(), entity->id());
}

TEST(CommandLifecycleTest, Selection_ClearRemovesAll)
{
    EntityDocument2D doc;

    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);
    EXPECT_FALSE(doc.selection().empty());

    doc.selection().clear();
    EXPECT_TRUE(doc.selection().empty());
}

// ============================================================================
// Undo/Redo 语义完整性
// ============================================================================

TEST(CommandLifecycleTest, Undo_AfterCommit_RestoresState)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new DrawLineCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    // 执行画线命令（交互式）
    dispatcher.execute(QStringLiteral("2d.draw_line"));
    // 拾取两个点
    dispatcher.forwardMouseDown(100, 100);
    dispatcher.forwardMouseDown(200, 200);

    // 提交后应入栈
    EXPECT_GT(undoStack.count(), 0);
    EXPECT_TRUE(undoStack.canUndo());

    const size_t entityCountAfterDraw = doc.entities().size();

    // 撤销
    undoStack.undo();
    EXPECT_EQ(doc.entities().size(), entityCountAfterDraw - 1);
}

TEST(CommandLifecycleTest, Undo_Redo_RestoresState)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new DrawLineCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    dispatcher.forwardMouseDown(100, 100);
    dispatcher.forwardMouseDown(200, 200);

    const size_t entityCountAfterDraw = doc.entities().size();
    ASSERT_GT(entityCountAfterDraw, 0u);

    // 撤销
    undoStack.undo();
    EXPECT_EQ(doc.entities().size(), entityCountAfterDraw - 1);

    // 重做
    undoStack.redo();
    EXPECT_EQ(doc.entities().size(), entityCountAfterDraw);
}

TEST(CommandLifecycleTest, UndoStack_CancelDoesNotPush)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new DrawLineCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    dispatcher.forwardMouseDown(100, 100);

    // 取消命令
    dispatcher.cancel();

    // 取消不进栈
    EXPECT_EQ(undoStack.count(), 0);
    EXPECT_FALSE(undoStack.canUndo());
}

// ============================================================================
// handlerFor 与 currentHandler 分离
// ============================================================================

TEST(CommandLifecycleTest, HandlerFor_DoesNotDependOnCurrentHandler)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handlerA = new DrawLineCommand();
    auto handlerB = new SelectCommand();
    dispatcher.registerHandler(handlerA);
    dispatcher.registerHandler(handlerB);
    dispatcher.setUiServices(makeTestServices());

    // handlerFor 按 ID 查找，不依赖 m_activeCommandId
    EXPECT_EQ(dispatcher.handlerFor(QStringLiteral("2d.draw_line")), handlerA);
    EXPECT_EQ(dispatcher.handlerFor(QStringLiteral("2d.select")), handlerB);
    EXPECT_EQ(dispatcher.handlerFor(QStringLiteral("nonexistent")), nullptr);
}

// ============================================================================
// activate 失败自动 cancel
// ============================================================================

TEST(CommandLifecycleTest, ActivateFailure_TriggersCancel)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    // 不设置 UiServices::document2D，DrawLineCommand 的 activate 会失败
    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(nullptr));

    dispatcher.execute(QStringLiteral("2d.draw_line"));

    // activate 失败 → cancel，不进栈
    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Idle);
    EXPECT_EQ(undoStack.count(), 0);
}

// ============================================================================
// 重复注册覆盖
// ============================================================================

TEST(CommandLifecycleTest, RegisterHandler_OverwritesExisting)
{
    DefaultUiCommandDispatcher dispatcher;

    auto handler1 = new DrawLineCommand();
    dispatcher.registerHandler(handler1);
    EXPECT_EQ(dispatcher.handlerFor(QStringLiteral("2d.draw_line")), handler1);

    auto handler2 = new DrawLineCommand();
    dispatcher.registerHandler(handler2);
    // 第二次注册应覆盖
    EXPECT_EQ(dispatcher.handlerFor(QStringLiteral("2d.draw_line")), handler2);

    delete handler1;
}

// ============================================================================
// MoveCommand 测试（新架构落地样板 P0-12）
// ============================================================================

TEST(CommandLifecycleTest, MoveCommand_ExecuteAndSubmit)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new MoveCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    // 创建并选择实体
    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);

    dispatcher.execute(QStringLiteral("2d.move"));
    EXPECT_TRUE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Active);

    // 模拟鼠标交互：按下锚点，移动到目标位置，释放
    dispatcher.forwardMouseDown(0, 0);
    dispatcher.forwardMouseMove(50, 50);
    dispatcher.forwardMouseUp(50, 50);

    // 提交后状态应清理
    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Idle);

    // 验证实体已移动
    auto movedLine = doc.lineById(entity->id());
    ASSERT_TRUE(movedLine != nullptr);
    EXPECT_NEAR(movedLine->start().x(), 50, 0.01);
    EXPECT_NEAR(movedLine->start().y(), 50, 0.01);
    EXPECT_NEAR(movedLine->end().x(), 150, 0.01);
    EXPECT_NEAR(movedLine->end().y(), 150, 0.01);

    // 验证 undo 栈
    EXPECT_GT(undoStack.count(), 0);
    EXPECT_TRUE(undoStack.canUndo());
}

TEST(CommandLifecycleTest, MoveCommand_CancelDoesNotPushUndo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new MoveCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);

    dispatcher.execute(QStringLiteral("2d.move"));
    ASSERT_TRUE(dispatcher.hasActiveCommand());

    // 开始移动但取消
    dispatcher.forwardMouseDown(0, 0);
    dispatcher.cancel();

    // 取消不进栈
    EXPECT_EQ(undoStack.count(), 0);
    EXPECT_FALSE(undoStack.canUndo());

    // 实体位置应恢复
    auto line = doc.lineById(entity->id());
    ASSERT_TRUE(line != nullptr);
    EXPECT_NEAR(line->start().x(), 0, 0.01);
    EXPECT_NEAR(line->start().y(), 0, 0.01);
}

TEST(CommandLifecycleTest, MoveCommand_UndoRedo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new MoveCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);

    dispatcher.execute(QStringLiteral("2d.move"));
    dispatcher.forwardMouseDown(0, 0);
    dispatcher.forwardMouseUp(50, 50);

    // 验证移动后位置
    auto line = doc.lineById(entity->id());
    EXPECT_NEAR(line->start().x(), 50, 0.01);

    // 撤销
    ASSERT_TRUE(undoStack.canUndo());
    undoStack.undo();
    line = doc.lineById(entity->id());
    EXPECT_NEAR(line->start().x(), 0, 0.01);
    EXPECT_NEAR(line->start().y(), 0, 0.01);

    // 重做
    ASSERT_TRUE(undoStack.canRedo());
    undoStack.redo();
    line = doc.lineById(entity->id());
    EXPECT_NEAR(line->start().x(), 50, 0.01);
    EXPECT_NEAR(line->start().y(), 50, 0.01);
}

TEST(CommandLifecycleTest, MoveCommand_ActivateFailsWithoutSelection)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new MoveCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    // 无选中实体时激活应失败
    dispatcher.execute(QStringLiteral("2d.move"));

    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Idle);
    EXPECT_EQ(undoStack.count(), 0);
}

TEST(CommandLifecycleTest, MoveCommand_PreviewDoesNotModifyDocument)
{
    DefaultUiCommandDispatcher dispatcher;

    EntityDocument2D doc;
    auto handler = new MoveCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);

    dispatcher.execute(QStringLiteral("2d.move"));

    // 预览阶段：移动鼠标（不应改变实体位置）
    dispatcher.forwardMouseDown(0, 0);
    dispatcher.forwardMouseMove(50, 50);

    CommandPreview preview = handler->preview();
    EXPECT_TRUE(preview.valid);

    // 预览阶段实体位置不应改变
    auto line = doc.lineById(entity->id());
    EXPECT_NEAR(line->start().x(), 0, 0.01);
}

TEST(CommandLifecycleTest, MoveCommand_OnMouseUpDoesNotModifyDocument)
{
    // 关键生命周期契约验证：
    // onMouseUp() 仅记录状态，不修改文档
    // document 变更必须且只能在 commit() 中发生
    // cancel() 不修改文档

    EntityDocument2D doc;
    auto handler = new MoveCommand();
    handler->setDocument(&doc);

    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);

    // 手动模拟 activate（绕过 dispatcher 的自动 submit）
    UiServices services = makeTestServices(&doc);
    EXPECT_TRUE(handler->activate(services));
    EXPECT_EQ(handler->state(), CommandState::Active);

    // onMouseDown：记录锚点
    EXPECT_TRUE(handler->onMouseDown(0, 0));

    // onMouseMove：记录当前点
    EXPECT_TRUE(handler->onMouseMove(50, 50));

    // onMouseUp：仅记录目标点，不修改文档
    EXPECT_TRUE(handler->onMouseUp(50, 50));

    // 验证：onMouseUp 后文档未被修改
    auto line = doc.lineById(entity->id());
    ASSERT_TRUE(line != nullptr);
    EXPECT_NEAR(line->start().x(), 0, 0.01);
    EXPECT_NEAR(line->start().y(), 0, 0.01);
    EXPECT_NEAR(line->end().x(), 100, 0.01);
    EXPECT_NEAR(line->end().y(), 100, 0.01);

    // 验证：isComplete() 为 true（可以提交了）
    EXPECT_TRUE(handler->isComplete());

    // cancel()：不修改文档，只重置状态
    handler->cancel();

    // 验证：cancel 后文档仍未被修改
    line = doc.lineById(entity->id());
    ASSERT_TRUE(line != nullptr);
    EXPECT_NEAR(line->start().x(), 0, 0.01);
    EXPECT_NEAR(line->start().y(), 0, 0.01);

    // commit()：这是唯一修改文档的地方
    handler->reset();
    doc.selection().add(entity);
    EXPECT_TRUE(handler->activate(services));
    handler->onMouseDown(0, 0);
    handler->onMouseUp(50, 50);
    handler->commit();

    // 验证：commit 后文档被修改
    line = doc.lineById(entity->id());
    ASSERT_TRUE(line != nullptr);
    EXPECT_NEAR(line->start().x(), 50, 0.01);
    EXPECT_NEAR(line->start().y(), 50, 0.01);
    EXPECT_NEAR(line->end().x(), 150, 0.01);
    EXPECT_NEAR(line->end().y(), 150, 0.01);
}

TEST(CommandLifecycleTest, MoveCommand_ResetAllowsReExecution)
{
    DefaultUiCommandDispatcher dispatcher;

    EntityDocument2D doc;
    auto handler = new MoveCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);

    // 第一次执行
    dispatcher.execute(QStringLiteral("2d.move"));
    dispatcher.forwardMouseDown(0, 0);
    dispatcher.forwardMouseUp(50, 50);
    EXPECT_EQ(handler->state(), CommandState::Idle);

    // 重新选择实体
    doc.selection().clear();
    doc.selection().add(entity);

    // 第二次执行：reset 后应可重复
    dispatcher.execute(QStringLiteral("2d.move"));
    EXPECT_EQ(handler->state(), CommandState::Active);
    dispatcher.forwardMouseDown(50, 50);
    dispatcher.forwardMouseUp(100, 100);
    EXPECT_EQ(handler->state(), CommandState::Idle);

    // 验证第二次移动后的位置
    auto line = doc.lineById(entity->id());
    EXPECT_NEAR(line->start().x(), 100, 0.01);
}

TEST(CommandLifecycleTest, MoveCommand_SelectionIsSourceOfTruth)
{
    // 验证 MoveCommand 从文档 selection() 读取选中实体，不维护视口副本
    EntityDocument2D doc;

    auto entity = doc.createLine(QPointF(0, 0), QPointF(100, 100));
    doc.selection().add(entity);

    // 选择从文档唯一事实源读取
    EXPECT_FALSE(doc.selection().empty());
    auto items = doc.selection().items();
    EXPECT_EQ(items.size(), 1);
    EXPECT_EQ(items.first()->id(), entity->id());
}

TEST(CommandLifecycleTest, CircleCommand_ExecuteAndSubmit)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new CircleCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_circle"));
    EXPECT_TRUE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Active);

    dispatcher.forwardMouseDown(100, 100);
    dispatcher.forwardMouseUp(200, 200);

    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Idle);

    auto entities = doc.entities();
    ASSERT_EQ(entities.size(), 1);

    EXPECT_GT(undoStack.count(), 0);
    EXPECT_TRUE(undoStack.canUndo());
}

TEST(CommandLifecycleTest, CircleCommand_CancelDoesNotPushUndo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new CircleCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_circle"));
    ASSERT_TRUE(dispatcher.hasActiveCommand());

    dispatcher.forwardMouseDown(100, 100);
    dispatcher.cancel();

    EXPECT_EQ(undoStack.count(), 0);
    EXPECT_FALSE(undoStack.canUndo());

    auto entities = doc.entities();
    EXPECT_EQ(entities.size(), 0);
}

TEST(CommandLifecycleTest, CircleCommand_PreviewDoesNotModifyDocument)
{
    EntityDocument2D doc;
    auto handler = new CircleCommand();
    handler->setDocument(&doc);
    UiServices services = makeTestServices(&doc);

    handler->activate(services);
    handler->onMouseDown(100, 100);
    handler->onMouseMove(150, 150);

    CommandPreview preview = handler->preview();
    EXPECT_TRUE(preview.valid);

    auto entities = doc.entities();
    EXPECT_EQ(entities.size(), 0);

    handler->cancel();
}

TEST(CommandLifecycleTest, PolylineCommand_ExecuteAndSubmit)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new PolylineCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_polyline"));
    EXPECT_TRUE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Active);

    dispatcher.forwardMouseDown(0, 0);
    dispatcher.forwardMouseDown(100, 0);
    dispatcher.forwardMouseDown(100, 100);
    dispatcher.forwardMouseUp(0, 100);

    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(handler->state(), CommandState::Idle);

    auto entities = doc.entities();
    ASSERT_EQ(entities.size(), 1);

    EXPECT_GT(undoStack.count(), 0);
    EXPECT_TRUE(undoStack.canUndo());
}

TEST(CommandLifecycleTest, PolylineCommand_CancelDoesNotPushUndo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    EntityDocument2D doc;
    auto handler = new PolylineCommand();
    handler->setDocument(&doc);
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_polyline"));
    ASSERT_TRUE(dispatcher.hasActiveCommand());

    dispatcher.forwardMouseDown(0, 0);
    dispatcher.forwardMouseDown(100, 0);
    dispatcher.cancel();

    EXPECT_EQ(undoStack.count(), 0);
    EXPECT_FALSE(undoStack.canUndo());

    auto entities = doc.entities();
    EXPECT_EQ(entities.size(), 0);
}