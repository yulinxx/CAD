#pragma once

#include <QString>
#include <QPointF>
#include <QList>
#include <QLineF>
#include <map>
#include <memory>
#include <vector>
#include <functional>

#include "UiServices.h"

class SceneDocument2D;
class UndoCommand;

namespace Eg
{
    struct SyEntity;
}

/**
 * @file UiCommandHandler.h
 * @brief 命令处理器接口定义
 *
 * 定义命令执行的状态机和生命周期接口，支持交互式命令。
 * 命令状态机：Idle → Active → Committed / Cancelled
 *
 * 命令与工具的职责分离：
 * - 命令（Command）：一次意图执行，负责生命周期管理（activate/commit/cancel）和业务提交
 * - 工具（Tool）：命令的交互载体，负责事件处理（拾点、预览、拖拽、约束）
 */

 /**
  * @class ITool
  * @brief 工具接口
  */
class ITool
{
public:
    virtual ~ITool() = default;

public:
    virtual QString toolId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool activate(const UiServices& services) = 0;
    virtual void cancel() = 0;
    virtual void reset() = 0;
    virtual bool onMouseDown(int x, int y)
    {
        (void)x; (void)y; return false;
    }
    virtual bool onMouseMove(int x, int y)
    {
        (void)x; (void)y; return false;
    }
    virtual bool onMouseUp(int x, int y)
    {
        (void)x; (void)y; return false;
    }
    virtual bool onKeyPress(int key)
    {
        (void)key; return false;
    }
    virtual bool isStageComplete() const
    {
        return false;
    }
    virtual QString currentStage() const
    {
        return QStringLiteral("idle");
    }
};

/**
 * @enum CommandState
 * @brief 命令状态枚举
 */
enum class CommandState
{
    Idle,
    Active,
    Committed,
    Cancelled
};

/**
 * @enum PreviewType
 * @brief 预览几何类型
 */
enum class PreviewType
{
    None,
    Line,
    Circle,
    Arc,
    Polyline,
    Polygon,
    Bezier2,
    Bezier,
    Nurbs,
    SmartLine
};

/**
 * @struct CommandPreview
 * @brief 命令预览数据，视口通过此结构体获取预览信息，无需感知具体命令类型
 */
struct CommandPreview
{
    bool valid{ false };
    PreviewType type{ PreviewType::None };
    QPointF previewStart;
    QPointF previewEnd;
    QPointF previewCenter;
    double previewRadius{ 0.0 };
    double previewStartAngle{ 0.0 };
    double previewEndAngle{ 0.0 };
    QVector<QPointF> previewPoints;
    QVector<QPointF> controlPoints;
    QString stageText;
};

/**
 * @class ICommandHandler
 * @brief 命令处理器抽象接口
 *
 * @deprecated 请优先使用 IOperation + OperationBus (UI/2D/Include/UI2D/Operation/IOperation.h) 体系。
 *             ICommandHandler 将在后续重构中逐步迁移至 OperationBus。
 *             新功能请勿新增 ICommandHandler 子类。
 */
class ICommandHandler
{
public:
    virtual ~ICommandHandler() = default;

public:
    virtual QString commandId() const = 0;
    virtual QString displayName() const = 0;
    virtual bool isInteractive() const = 0;
    virtual CommandState state() const = 0;
    virtual bool activate(const UiServices& services) = 0;
    virtual void cancel() = 0;
    virtual void commit() = 0;
    virtual bool onMouseDown(int x, int y)
    {
        (void)x; (void)y; return false;
    }
    virtual bool onMouseMove(int x, int y)
    {
        (void)x; (void)y; return false;
    }
    virtual bool onMouseUp(int x, int y)
    {
        (void)x; (void)y; return false;
    }
    virtual bool onKeyPress(int key)
    {
        (void)key; return false;
    }
    virtual bool onWheel(int delta)
    {
        (void)delta; return false;
    }
    virtual void reset() = 0;
    virtual ITool* activeTool() const
    {
        return nullptr;
    }
    virtual UndoCommand* createUndoCommand()
    {
        return nullptr;
    }
    virtual bool isComplete() const
    {
        return false;
    }
    virtual CommandPreview preview() const
    {
        return {};
    }
};

/**
 * @class UndoCommand
 * @brief 可撤销操作基类
 */
class UndoCommand
{
public:
    explicit UndoCommand(const QString& text);
    virtual ~UndoCommand() = default;

    QString text() const;
    virtual void undo() = 0;
    virtual void redo() = 0;

private:
    QString m_text;
};

/**
 * @class IUndoStack
 * @brief 撤销栈接口
 */
class IUndoStack
{
public:
    virtual ~IUndoStack() = default;

public:
    virtual void push(UndoCommand* command) = 0;
    virtual bool undo() = 0;
    virtual bool redo() = 0;
    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual QString undoText() const = 0;
    virtual QString redoText() const = 0;
    virtual void clear() = 0;
    virtual int count() const = 0;

    using RefreshCallback = std::function<void()>;
    virtual void setRefreshCallback(RefreshCallback callback) = 0;
};

/**
 * @class DefaultUndoStack
 * @brief 默认撤销栈实现
 */
class DefaultUndoStack final : public IUndoStack
{
public:
    void push(UndoCommand* command) override;
    bool undo() override;
    bool redo() override;
    bool canUndo() const override;
    bool canRedo() const override;
    QString undoText() const override;
    QString redoText() const override;
    void clear() override;
    int count() const override;
    void setRefreshCallback(RefreshCallback callback) override;

private:
    void notifyRefresh();

    std::vector<std::unique_ptr<UndoCommand>> m_stack;
    int m_currentIndex{ -1 };
    RefreshCallback m_refreshCallback;
    bool m_isNotifying{ false };
};

/**
 * @struct EntitySnapshot
 * @brief 实体几何快照
 *
 * 用于 Delete/Mirror/Copy/Arc/Polyline/Rotate 等命令的 undo/redo 支持。
 */
struct EntitySnapshot
{
    QString id;
    int type{ 0 };
    QPointF basePoint;
    QVector<QPointF> points;
    double radius{ 0.0 };
    double startAngle{ 0.0 };
    double endAngle{ 0.0 };
    bool closed{ false };
    bool ccw{ true };
};

class SnapshotUndoCommand : public UndoCommand
{
public:
    SnapshotUndoCommand(const QString& text, SceneDocument2D* document, const QVector<EntitySnapshot>& snapshots);
    void undo() override;
    void redo() override;

private:
    SceneDocument2D* m_document{ nullptr };
    QVector<EntitySnapshot> m_snapshots;
    std::vector<std::unique_ptr<Eg::SyEntity>> m_storedEntities;
};

/**
 * @class PointPickerTool
 * @brief 拾点工具
 */
class PointPickerTool : public ITool
{
public:
    explicit PointPickerTool(int requiredPoints = 2);

    QString toolId() const override;
    QString displayName() const override;
    bool activate(const UiServices& services) override;
    void cancel() override;
    void reset() override;

    bool onMouseDown(int x, int y) override;
    bool isStageComplete() const override;
    QString currentStage() const override;

    const QPointF& lastPoint() const
    {
        return m_lastPoint;
    }
    const std::vector<QPointF>& pickedPoints() const
    {
        return m_pickedPoints;
    }
    bool hasEnoughPoints() const
    {
        return m_pickedPoints.size() >= static_cast<size_t>(m_requiredPoints);
    }

private:
    int m_requiredPoints;
    std::vector<QPointF> m_pickedPoints;
    QPointF m_lastPoint;
    UiServices m_services;
};

/**
 * @class SimpleCommandHandler
 * @brief 简单命令处理器实现
 */
class SimpleCommandHandler : public ICommandHandler
{
public:
    explicit SimpleCommandHandler(const QString& commandId, const QString& displayName);

    QString commandId() const override;
    QString displayName() const override;
    bool isInteractive() const override;
    CommandState state() const override;
    bool activate(const UiServices& services) override;
    void cancel() override;
    void commit() override;
    void reset() override;

private:
    QString m_commandId;
    QString m_displayName;
    CommandState m_state{ CommandState::Idle };
};