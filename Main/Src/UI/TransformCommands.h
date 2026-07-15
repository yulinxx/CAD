#pragma once

#include "UiCommandHandler.h"

class SceneDocument2D;
class ISelectionService;

/**
 * @class MoveUndoCommand
 * @brief 移动命令的撤销操作
 */
class MoveUndoCommand : public UndoCommand
{
public:
    MoveUndoCommand(SceneDocument2D* document,
        std::map<QString, std::vector<QPointF>> originalPositions);

    void undo() override;
    void redo() override;

private:
    SceneDocument2D* m_document{ nullptr };
    std::map<QString, std::vector<QPointF>> m_originalPositions;
    std::map<QString, std::vector<QPointF>> m_newPositions;
};

/**
 * @enum UndoMode
 * @brief 撤销模式：区分复制/删除操作方向
 */
enum class UndoMode
{
    Copy,   // 撤销时删除新建实体，重做时恢复
    Delete  // 撤销时恢复已删实体，重做时再次删除
};

/**
 * @class CopyUndoCommand
 * @brief 复制命令的撤销操作（也用于删除命令的撤销/重做）
 */
class CopyUndoCommand : public UndoCommand
{
public:
    CopyUndoCommand(const QString& text, SceneDocument2D* document,
        const QVector<EntitySnapshot>& snapshots,
        const QStringList& entityIds,
        UndoMode mode,
        ISelectionService* selService = nullptr);
    void undo() override;
    void redo() override;
private:
    SceneDocument2D* m_document{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    QVector<EntitySnapshot> m_snapshots;     // 实体快照（undo/redo 时用于重建）
    QStringList m_entityIds;                 // 关联的实体 ID 列表
    UndoMode m_mode{ UndoMode::Copy };
    std::vector<std::unique_ptr<Eg::SyEntity>> m_storedEntities;
};

/**
 * @class MoveCommand
 * @brief 移动命令
 */
class MoveCommand : public ICommandHandler
{
public:
    MoveCommand();

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

private:
    void saveOriginalPositions();
    void restoreOriginalPositions();

    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    QPointF m_anchorPoint;
    QPointF m_targetPoint;
    bool m_hasAnchor{ false };
    bool m_committed{ false };
    std::map<QString, std::vector<QPointF>> m_originalPositions;
};

/**
 * @class RotateCommand
 * @brief 旋转命令（支持用户拾取旋转中心）
 */
class RotateCommand : public ICommandHandler
{
public:
    RotateCommand();

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
    bool onKeyPress(int key) override;

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(SceneDocument2D* document);

    const QPointF& rotationCenter() const
    {
        return m_rotationCenter;
    }
    double startAngle() const
    {
        return m_startAngle;
    }
    double currentAngle() const
    {
        return m_currentAngle;
    }
    bool isRotating() const
    {
        return m_state == CommandState::Active && m_stage >= 2;
    }
    int stage() const
    {
        return m_stage;
    }

private:
    void restoreOriginalPoints();
    void computeDefaultCenter();

    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    QPointF m_rotationCenter;
    QPointF m_startPoint;
    double m_startAngle{ 0.0 };
    double m_currentAngle{ 0.0 };
    QString m_selectedEntityId;
    std::vector<QPointF> m_originalPoints;
    QVector<EntitySnapshot> m_originalSnapshots;
    int m_stage{ 0 };
};

/**
 * @class CopyCommand
 * @brief 复制命令
 */
class CopyCommand : public ICommandHandler
{
public:
    CopyCommand();

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
    bool onKeyPress(int key) override;

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(SceneDocument2D* document);

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    QPointF m_anchorPoint;
    QPointF m_targetPoint;
    bool m_hasAnchor{ false };
    int m_stage{ 0 };
    QStringList m_copiedEntityIds;
    QVector<EntitySnapshot> m_copiedSnapshots;
};

/**
 * @class DeleteCommand
 * @brief 删除命令
 */
class DeleteCommand : public ICommandHandler
{
public:
    DeleteCommand();

    QString commandId() const override;
    QString displayName() const override;
    bool isInteractive() const override;
    CommandState state() const override;
    bool activate(const UiServices& services) override;
    void cancel() override;
    void commit() override;
    void reset() override;

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(SceneDocument2D* document);

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    QStringList m_deletedEntityIds;
    QVector<EntitySnapshot> m_snapshots;
    bool m_committed{ false };
};

/**
 * @class MirrorCommand
 * @brief 镜像命令
 */
class MirrorCommand : public ICommandHandler
{
public:
    MirrorCommand();

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
    bool onKeyPress(int key) override;

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(SceneDocument2D* document);

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    ISelectionService* m_selectionService{ nullptr };
    QPointF m_mirrorStart;
    QPointF m_mirrorEnd;
    int m_stage{ 0 };
    QStringList m_mirroredEntityIds;
    QVector<EntitySnapshot> m_originalSnapshots;
    QVector<EntitySnapshot> m_mirroredSnapshots;
};