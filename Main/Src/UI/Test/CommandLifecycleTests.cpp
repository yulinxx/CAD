#include "UI/UiCommandDispatcher.h"
#include "UI/UiCommandHandler.h"
#include "UI/CreateCommands.h"
#include "UI/TransformCommands.h"
#include "UI/SelectCommands.h"
#include "SceneDocument2D.h"
#include "UI/UiServices.h"
#include "Engine2D/Core/SceneManager.h"
#include "Engine2D/SyEntity/SyLine.h"

#include <gtest/gtest.h>

#include <memory>

namespace
{
    UiServices makeTestServices(SceneDocument2D* doc = nullptr)
    {
        UiServices svc;
        svc.document2D = doc;
        return svc;
    }
}

TEST(CommandLifecycleTest, ExecuteNonInteractive_CommitsAndPushesUndo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new SimpleCommandHandler(QStringLiteral("test.non_interactive"), QStringLiteral("Test"));
    dispatcher.registerHandler(handler);
    dispatcher.setUiServices(makeTestServices());

    EXPECT_EQ(undoStack.count(), 0);
    EXPECT_FALSE(dispatcher.hasActiveCommand());

    dispatcher.execute(QStringLiteral("test.non_interactive"));

    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(undoStack.count(), 0);
}

TEST(CommandLifecycleTest, ExecuteInteractive_StaysActive)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    EXPECT_FALSE(dispatcher.hasActiveCommand());

    dispatcher.execute(QStringLiteral("2d.draw_line"));

    EXPECT_TRUE(dispatcher.hasActiveCommand());
    EXPECT_EQ(undoStack.count(), 0);
}

TEST(CommandLifecycleTest, CommitInteractive_PushesUndoAndResets)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    EXPECT_TRUE(dispatcher.hasActiveCommand());

    handler->onMouseDown(10, 20);
    EXPECT_FALSE(handler->isComplete());

    handler->onMouseDown(100, 200);
    EXPECT_TRUE(handler->isComplete());

    dispatcher.submit();

    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(undoStack.count(), 1);

    EXPECT_FALSE(doc.selectedIdsQ().isEmpty());
}

TEST(CommandLifecycleTest, CancelInteractive_DoesNotPushUndo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    EXPECT_TRUE(dispatcher.hasActiveCommand());

    dispatcher.cancel();

    EXPECT_FALSE(dispatcher.hasActiveCommand());
    EXPECT_EQ(undoStack.count(), 0);
}

TEST(CommandLifecycleTest, Reset_EnablesReExecute)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(10, 20);
    dispatcher.cancel();

    EXPECT_FALSE(dispatcher.hasActiveCommand());

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    EXPECT_TRUE(dispatcher.hasActiveCommand());
}

TEST(CommandLifecycleTest, Undo_RemovesEntityAndRedo_RestoresIt)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(10, 20);
    handler->onMouseDown(100, 200);
    dispatcher.submit();

    const size_t initialEntityCount = doc.sceneManager()->getAllEntities().size();
    EXPECT_EQ(initialEntityCount, 1);

    undoStack.undo();
    EXPECT_EQ(doc.sceneManager()->getAllEntities().size(), initialEntityCount - 1);

    undoStack.redo();
    EXPECT_EQ(doc.sceneManager()->getAllEntities().size(), initialEntityCount);
}

TEST(CommandLifecycleTest, DoubleCommit_OnlyPushesOneUndo)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(10, 20);
    handler->onMouseDown(100, 200);
    dispatcher.submit();

    handler->commit();
    EXPECT_EQ(undoStack.count(), 1);
    EXPECT_FALSE(dispatcher.hasActiveCommand());
}

TEST(CommandLifecycleTest, CancelAfterMouseDown_DoesNotAddEntity)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(10, 20);
    dispatcher.cancel();

    EXPECT_EQ(doc.sceneManager()->getAllEntities().size(), 0);
    EXPECT_EQ(undoStack.count(), 0);
}

TEST(CommandLifecycleTest, CancelMidLine_RestoresSelection)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(10, 20);
    EXPECT_TRUE(doc.selectedIdsQ().isEmpty());

    dispatcher.cancel();
    EXPECT_FALSE(dispatcher.hasActiveCommand());
}

TEST(CommandLifecycleTest, RedoAfterCommit_SelectsEntity)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(10, 20);
    handler->onMouseDown(100, 200);
    dispatcher.submit();

    EXPECT_FALSE(doc.selectedIdsQ().isEmpty());

    undoStack.undo();
    EXPECT_TRUE(doc.selectedIdsQ().isEmpty());

    undoStack.redo();
    EXPECT_FALSE(doc.selectedIdsQ().isEmpty());
}

TEST(CommandLifecycleTest, SelectCommand_ChangesSelection)
{
    DefaultUndoStack undoStack;

    SceneDocument2D doc;
    QString id1 = doc.createLine(QPointF(0, 0), QPointF(10, 10));
    QString id2 = doc.createLine(QPointF(20, 20), QPointF(30, 30));

    SelectCommand select;
    select.setDocument(&doc);
    select.activate(makeTestServices(&doc));
    select.setSelectedEntityId(id1);
    select.commit();

    doc.clearSelection();
    doc.selectEntity(id1);
    EXPECT_EQ(doc.selectedIdsQ().size(), 1);
}

TEST(CommandLifecycleTest, RedoAfterMove_EntityReturnsToNewPosition)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new MoveCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    QString id = doc.createLine(QPointF(0, 0), QPointF(10, 10));
    doc.clearSelection();
    doc.selectEntity(id);

    dispatcher.execute(QStringLiteral("2d.move"));
    handler->onMouseDown(0, 0);
    handler->onMouseMove(5, 10);
    handler->onMouseUp(5, 10);
    dispatcher.submit();
    EXPECT_EQ(undoStack.count(), 1);

    auto* entity = doc.entityByStringId(id);
    ASSERT_TRUE(entity != nullptr);
    ASSERT_EQ(entity->eType, Eg::EType::LINE);
    auto* line = static_cast<Eg::SyLine*>(entity);
    EXPECT_DOUBLE_EQ(line->vPoints[0].x(), 5.0);
    EXPECT_DOUBLE_EQ(line->vPoints[0].y(), 10.0);
    EXPECT_DOUBLE_EQ(line->vPoints[1].x(), 15.0);
    EXPECT_DOUBLE_EQ(line->vPoints[1].y(), 20.0);

    undoStack.undo();
    entity = doc.entityByStringId(id);
    ASSERT_TRUE(entity != nullptr);
    line = static_cast<Eg::SyLine*>(entity);
    EXPECT_DOUBLE_EQ(line->vPoints[0].x(), 0.0);
    EXPECT_DOUBLE_EQ(line->vPoints[0].y(), 0.0);

    undoStack.redo();
    entity = doc.entityByStringId(id);
    ASSERT_TRUE(entity != nullptr);
    line = static_cast<Eg::SyLine*>(entity);
    EXPECT_DOUBLE_EQ(line->vPoints[0].x(), 5.0);
    EXPECT_DOUBLE_EQ(line->vPoints[0].y(), 10.0);
}

TEST(CommandLifecycleTest, SelectCommand_BoxSelect_MultipleEntities)
{
    SceneDocument2D doc;

    QString id1 = doc.createLine(QPointF(0, 0), QPointF(10, 10));
    QString id2 = doc.createLine(QPointF(100, 100), QPointF(110, 110));

    SelectCommand select;
    select.setDocument(&doc);
    select.activate(makeTestServices(&doc));
    select.onMouseDown(0, 0);
    select.onMouseMove(50, 50);
    select.onMouseUp(50, 50);
    select.commit();

    EXPECT_FALSE(doc.selectedIdsQ().isEmpty());
}

TEST(CommandLifecycleTest, DrawLine_UndoRemovesRedoRestores)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(0, 0);
    handler->onMouseDown(10, 10);
    dispatcher.submit();
    EXPECT_EQ(undoStack.count(), 1);

    const size_t countAfterDraw = doc.sceneManager()->getAllEntities().size();

    undoStack.undo();
    EXPECT_EQ(doc.sceneManager()->getAllEntities().size(), countAfterDraw - 1);

    undoStack.redo();
    EXPECT_EQ(doc.sceneManager()->getAllEntities().size(), countAfterDraw);
}

TEST(CommandLifecycleTest, Move_UndoRedo_SelectionPreserved)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new MoveCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    QString id = doc.createLine(QPointF(0, 0), QPointF(10, 10));
    doc.clearSelection();
    doc.selectEntity(id);

    dispatcher.execute(QStringLiteral("2d.move"));
    handler->onMouseDown(0, 0);
    handler->onMouseMove(20, 30);
    handler->onMouseUp(20, 30);
    dispatcher.submit();
    EXPECT_EQ(undoStack.count(), 1);

    auto* entity = doc.entityByStringId(id);
    ASSERT_TRUE(entity != nullptr);
    ASSERT_EQ(entity->eType, Eg::EType::LINE);
    auto* line = static_cast<Eg::SyLine*>(entity);
    EXPECT_DOUBLE_EQ(line->vPoints[0].x(), 20.0);
    EXPECT_DOUBLE_EQ(line->vPoints[0].y(), 30.0);
    EXPECT_DOUBLE_EQ(line->vPoints[1].x(), 30.0);
    EXPECT_DOUBLE_EQ(line->vPoints[1].y(), 40.0);

    undoStack.undo();
    entity = doc.entityByStringId(id);
    ASSERT_TRUE(entity != nullptr);
    line = static_cast<Eg::SyLine*>(entity);
    EXPECT_DOUBLE_EQ(line->vPoints[0].x(), 0.0);
    EXPECT_DOUBLE_EQ(line->vPoints[0].y(), 0.0);

    undoStack.redo();
    entity = doc.entityByStringId(id);
    ASSERT_TRUE(entity != nullptr);
    line = static_cast<Eg::SyLine*>(entity);
    EXPECT_DOUBLE_EQ(line->vPoints[0].x(), 20.0);
    EXPECT_DOUBLE_EQ(line->vPoints[0].y(), 30.0);

    EXPECT_FALSE(doc.selectedIdsQ().isEmpty());
}

TEST(CommandLifecycleTest, SelectCommand_BoxSelectNoEntities)
{
    SceneDocument2D doc;

    SelectCommand select;
    select.setDocument(&doc);
    select.activate(makeTestServices(&doc));
    select.onMouseDown(0, 0);
    select.onMouseMove(5, 5);
    select.onMouseUp(5, 5);
    select.commit();

    EXPECT_TRUE(doc.selectedIdsQ().isEmpty());
}

TEST(CommandLifecycleTest, DrawLine_UndoRedo_SelectionTracks)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(0, 0);
    handler->onMouseDown(10, 10);
    dispatcher.submit();

    auto entities = doc.sceneManager()->getAllEntities();
    ASSERT_EQ(entities.size(), 1);

    undoStack.undo();
    entities = doc.sceneManager()->getAllEntities();
    EXPECT_EQ(entities.size(), 0);

    undoStack.redo();
    entities = doc.sceneManager()->getAllEntities();
    EXPECT_EQ(entities.size(), 1);
}

TEST(CommandLifecycleTest, DrawLine_UndoRedo_SelectionFromDoc)
{
    DefaultUndoStack undoStack;
    DefaultUiCommandDispatcher dispatcher;
    dispatcher.setUndoStack(&undoStack);

    auto handler = new DrawLineCommand();
    dispatcher.registerHandler(handler);

    SceneDocument2D doc;
    handler->setDocument(&doc);
    dispatcher.setUiServices(makeTestServices(&doc));

    dispatcher.execute(QStringLiteral("2d.draw_line"));
    handler->onMouseDown(0, 0);
    handler->onMouseDown(10, 10);
    dispatcher.submit();

    EXPECT_FALSE(doc.selectedIdsQ().isEmpty());

    undoStack.undo();
    EXPECT_TRUE(doc.selectedIdsQ().isEmpty());

    undoStack.redo();
    EXPECT_FALSE(doc.selectedIdsQ().isEmpty());
}