#pragma once

#include "UiCommandHandler.h"

#include <memory>

namespace Eg { struct SyEntity; }
class SceneDocument2D;

/**
 * @class DrawLineCommand
 * @brief 画线命令
 */
class DrawLineCommand : public ICommandHandler
{
public:
    DrawLineCommand();

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
    PointPickerTool m_pointPicker;
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    QPointF m_previewStart;
    QPointF m_previewEnd;
    QString m_createdEntityId;
};

/**
 * @class CircleCommand
 * @brief 画圆命令
 */
class CircleCommand : public ICommandHandler
{
public:
    CircleCommand();

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
    QPointF m_center;
    QPointF m_endPoint;
    QString m_createdEntityId;
    bool m_hasCenter{ false };
};

/**
 * @class ArcCommand
 * @brief 画弧命令
 */
class ArcCommand : public ICommandHandler
{
public:
    ArcCommand();

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
    QPointF m_center;
    QPointF m_startPoint;
    QPointF m_endPoint;
    QString m_createdEntityId;
    int m_stage{ 0 };
};

/**
 * @class PolylineCommand
 * @brief 画多段线命令
 */
class PolylineCommand : public ICommandHandler
{
public:
    PolylineCommand();

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
    void finish();

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    QVector<QPointF> m_points;
    QPointF m_currentPoint;
    bool m_completed{ false };
    QString m_createdEntityId;
};

/**
 * @class PolygonCommand
 * @brief 画多边形命令
 */
class PolygonCommand : public ICommandHandler
{
public:
    PolygonCommand();

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
    void finish();

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    SceneDocument2D* m_document{ nullptr };
    QVector<QPointF> m_points;
    QPointF m_currentPoint;
    bool m_completed{ false };
    QString m_createdEntityId;
};

/**
 * @class CircleUndoCommand
 * @brief 圆命令的撤销操作
 */
class CircleUndoCommand : public UndoCommand
{
public:
    CircleUndoCommand(SceneDocument2D* document, const QString& entityId);
    void undo() override;
    void redo() override;
private:
    SceneDocument2D* m_document{ nullptr };
    QString m_entityId;
    QPointF m_center;
    double m_radius{ 0.0 };
    std::unique_ptr<Eg::SyEntity> m_storedEntity;
};

class LineUndoCommand : public UndoCommand
{
public:
    LineUndoCommand(SceneDocument2D* document, const QString& entityId);
    void undo() override;
    void redo() override;
private:
    SceneDocument2D* m_document{ nullptr };
    QString m_entityId;
    QPointF m_start;
    QPointF m_end;
    std::unique_ptr<Eg::SyEntity> m_storedEntity;
};

class ArcUndoCommand : public UndoCommand
{
public:
    ArcUndoCommand(SceneDocument2D* document, const QString& entityId);
    void undo() override;
    void redo() override;
private:
    SceneDocument2D* m_document{ nullptr };
    QString m_entityId;
    QPointF m_center;
    double m_radius{ 0.0 };
    double m_startAngle{ 0.0 };
    double m_endAngle{ 0.0 };
    std::unique_ptr<Eg::SyEntity> m_storedEntity;
};

/**
 * @class PolylineUndoCommand
 * @brief 多段线命令的撤销操作
 */
class PolylineUndoCommand : public UndoCommand
{
public:
    PolylineUndoCommand(SceneDocument2D* document, const QString& entityId);
    PolylineUndoCommand(SceneDocument2D* document, const EntitySnapshot& snapshot);
    void undo() override;
    void redo() override;
private:
    SceneDocument2D* m_document{ nullptr };
    QString m_entityId;
    QVector<QPointF> m_points;
    EntitySnapshot m_snapshot;
    std::unique_ptr<Eg::SyEntity> m_storedEntity;
};