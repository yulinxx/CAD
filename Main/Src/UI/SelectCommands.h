#pragma once

#include "UiCommandHandler.h"

class SceneDocument2D;

/**
 * @class SelectCommand
 * @brief 选择命令
 */
class SelectCommand : public ICommandHandler
{
public:
    SelectCommand();

    QString commandId() const override;
    QString displayName() const override;
    bool isInteractive() const override;
    CommandState state() const override;
    bool activate(const UiServices& services) override;
    void cancel() override;
    void commit() override;
    void reset() override;

    bool onMouseDown(int x, int y) override;
    bool onMouseMove(int x, int y) override;
    bool onMouseUp(int x, int y) override;

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(SceneDocument2D* document);
    void setSelectedEntityId(const QString& entityId);

private:
    void performBoxSelect();

    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    QString m_selectedEntityId;
    QString m_oldSelectedId;
    bool m_boxSelecting{ false };
    QPointF m_boxSelectStart;
    QPointF m_boxSelectEnd;
};

/**
 * @class SelectUndoCommand
 * @brief 选择命令的撤销操作
 */
class SelectUndoCommand : public UndoCommand
{
public:
    SelectUndoCommand(SceneDocument2D* document, const QString& oldId, const QString& newId);
    void undo() override;
    void redo() override;

private:
    SceneDocument2D* m_document{ nullptr };
    QString m_oldId;
    QString m_newId;
};