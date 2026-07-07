#pragma once

#include <QString>
#include <QPointF>
#include <QList>
#include <QLineF>
#include <map>
#include <memory>
#include <vector>

#include "UiServices.h"
#include "UiEntities.h"

class EntityDocument2D;
class UndoCommand;
class ITransformable;

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
 *
 * 统一命令生命周期协议：
 *   execute(commandId)
 *     → handlerFor(commandId)          // 按 ID 查找 handler，不依赖 currentHandler
 *     → handler->reset()               // 重置到 Idle 状态
 *     → begin(commandId)               // 同步状态中心，标记 busy
 *     → handler->activate(services)    // 激活命令和工具
 *     → [事件循环阶段]                  // 交互式命令等待用户输入，非交互式直接提交
 *     → handler->commit()              // 提交业务逻辑（由 submit() 统一调用）
 *     → submit()                       // 压入 undo 栈 + 清理状态中心 + reset
 *   或
 *     → handler->cancel()              // 取消业务逻辑
 *     → cancel()                       // 清理状态中心 + reset（不压栈）
 */

/**
 * @class ITool
 * @brief 工具接口
 *
 * 工具是命令的交互载体，负责处理用户输入事件（鼠标、键盘），
 * 管理交互状态（拾点、预览、拖拽、约束），并生成预览反馈。
 *
 * 工具可以被多个命令复用：
 * - 拾点工具（PointPicker）：被画线、画圆、移动等命令共用
 * - 选择工具（Selector）：被移动、旋转、缩放等命令共用
 * - 约束工具（Constraint）：被各种绘图命令共用
 */
class ITool
{
public:
    virtual ~ITool() = default;

public:
    /// 获取工具 ID
    virtual QString toolId() const = 0;

    /// 获取工具显示名称
    virtual QString displayName() const = 0;

    /// 激活工具
    /// @param services UI 服务集合
    virtual bool activate(const UiServices& services) = 0;

    /// 取消工具
    virtual void cancel() = 0;

    /// 重置工具状态
    virtual void reset() = 0;

    /// 处理鼠标按下事件
    /// @param x 鼠标 X 坐标（视口坐标）
    /// @param y 鼠标 Y 坐标（视口坐标）
    /// @return 是否处理了事件
    virtual bool onMouseDown(int x, int y) { (void)x; (void)y; return false; }

    /// 处理鼠标移动事件
    /// @param x 鼠标 X 坐标（视口坐标）
    /// @param y 鼠标 Y 坐标（视口坐标）
    /// @return 是否处理了事件
    virtual bool onMouseMove(int x, int y) { (void)x; (void)y; return false; }

    /// 处理鼠标释放事件
    /// @param x 鼠标 X 坐标（视口坐标）
    /// @param y 鼠标 Y 坐标（视口坐标）
    /// @return 是否处理了事件
    virtual bool onMouseUp(int x, int y) { (void)x; (void)y; return false; }

    /// 处理键盘按键事件
    /// @param key 按键代码
    /// @return 是否处理了事件
    virtual bool onKeyPress(int key) { (void)key; return false; }

    /// 判断工具是否完成了当前阶段
    /// @return true 表示工具已完成当前阶段，可以进入下一阶段或提交
    virtual bool isStageComplete() const { return false; }

    /// 获取当前阶段描述
    /// @return 阶段描述文本
    virtual QString currentStage() const { return QStringLiteral("idle"); }
};

/**
 * @enum CommandState
 * @brief 命令状态枚举
 */
enum class CommandState
{
    Idle,      ///< 空闲状态，等待激活
    Active,    ///< 活动状态，正在执行交互操作
    Committed, ///< 已提交状态，命令完成并已压入撤销栈
    Cancelled  ///< 已取消状态，命令被取消
};

/**
 * @struct CommandPreview
 * @brief 命令预览数据，视口通过此结构体获取预览信息，无需感知具体命令类型
 *
 * 设计目标：让视口不再依赖具体命令类（如 DrawLineCommand），
 * 通过统一接口获取预览几何数据，实现视口与命令的解耦。
 */
struct CommandPreview
{
    bool valid{ false };          ///< 预览是否有效
    QPointF previewStart;         ///< 预览起点（视口坐标）
    QPointF previewEnd;           ///< 预览终点（视口坐标）
    QString stageText;            ///< 当前阶段提示文本
};

/**
 * @class ICommandHandler
 * @brief 命令处理器抽象接口
 *
 * 定义命令的完整生命周期：激活、取消、提交。
 * 交互式命令（如画线）在激活后会进入交互状态，等待用户输入。
 *
 * 命令与工具的关系：
 * - 命令持有一个或多个工具的引用
 * - 命令激活时，同时激活其关联的工具
 * - 用户输入事件先到达命令，再转发给当前活动的工具
 * - 工具完成阶段后，通知命令进行状态转换或提交
 *
 * 接口契约：
 *   execute()     → 创建/准备命令上下文，不直接做最终提交，不负责UI细节
 *   handlerFor()  → 负责把输入事件路由到正确handler，不做业务判断
 *   begin()       → 初始化命令状态，同步状态中心，标记busy
 *   activate()    → 进入交互阶段，激活关联工具，开始响应事件
 *   isComplete()  → 只判断"是否满足提交条件"，不做提交动作
 *   submit()      → 做最终校验和落库，只在成功时允许commit
 *   commit()      → 真正改变文档状态，入undo栈，触发刷新
 *   cancel()      → 取消业务逻辑，不进栈，清理临时状态
 *   reset()       → 清理所有临时状态，确保下次可重入
 *
 * Undo 语义边界：
 * - 绘图命令（DrawLine 等）：必须可 undo，commit() 后压栈
 * - 变换命令（Rotate 等）：必须可 undo，commit() 后压栈
 * - 选择命令（Select）：可 undo，commit() 后压栈
 * - 纯视图命令（Zoom/Pan）：不可 undo，不进栈
 * - cancel() 一律不进栈
 * - 预览阶段不进栈
 * - submit() 失败时（createUndoCommand 返回 nullptr）不进栈
 *
 * 状态机与视图刷新边界：
 * - commit() 后统一发刷新信号（由 Dispatcher::submit() 统一触发）
 * - 选择变化统一发选择变更信号（由 EntityDocument2D::selection() 统一管理）
 * - 预览变化只刷新预览层（通过 CommandPreview 接口，不影响文档）
 * - 命令状态变化不直接操作 UI 细节（通过 StateCenter 统一同步）
 */
class ICommandHandler
{
public:
    virtual ~ICommandHandler() = default;

public:
    /// 获取命令 ID
    /// @return 命令标识符
    virtual QString commandId() const = 0;

    /// 获取命令显示名称
    /// @return 命令显示名称
    virtual QString displayName() const = 0;

    /// 判断命令是否为交互式命令
    /// @return true 表示交互式命令，需要等待用户输入
    virtual bool isInteractive() const = 0;

    /// 获取当前命令状态
    /// @return 当前命令状态
    virtual CommandState state() const = 0;

    /// 激活命令
    /// @param services UI 服务集合
    /// @return 是否激活成功
    virtual bool activate(const UiServices& services) = 0;

    /// 取消命令
    /// 取消后命令状态变为 Cancelled，不会产生撤销操作，不进 undo 栈
    virtual void cancel() = 0;

    /// 提交命令
    /// 提交后命令状态变为 Committed，会产生撤销操作并压入撤销栈
    virtual void commit() = 0;

    /// 处理鼠标按下事件
    /// @param x 鼠标 X 坐标
    /// @param y 鼠标 Y 坐标
    /// @return 是否处理了事件
    virtual bool onMouseDown(int x, int y) { (void)x; (void)y; return false; }

    /// 处理鼠标移动事件
    /// @param x 鼠标 X 坐标
    /// @param y 鼠标 Y 坐标
    /// @return 是否处理了事件
    virtual bool onMouseMove(int x, int y) { (void)x; (void)y; return false; }

    /// 处理鼠标释放事件
    /// @param x 鼠标 X 坐标
    /// @param y 鼠标 Y 坐标
    /// @return 是否处理了事件
    virtual bool onMouseUp(int x, int y) { (void)x; (void)y; return false; }

    /// 处理键盘按键事件
    /// @param key 按键代码
    /// @return 是否处理了事件
    virtual bool onKeyPress(int key) { (void)key; return false; }

    /// 重置命令状态到 Idle
    virtual void reset() = 0;

    /// 获取当前活动的工具
    /// @return 当前活动工具指针，无活动工具时返回 nullptr
    virtual ITool* activeTool() const { return nullptr; }

    /// 创建撤销命令
    /// 命令提交时调用此方法获取可撤销操作，压入撤销栈
    /// @return 撤销命令实例，不需要撤销时返回 nullptr
    virtual UndoCommand* createUndoCommand() { return nullptr; }

    /// 判断命令是否已完成（交互式命令在收集完所需输入后返回 true）
    /// Dispatcher 在每次事件转发后检查此方法，若为 true 则自动调用 submit()
    /// @return true 表示命令已完成，可以提交
    virtual bool isComplete() const { return false; }

    /// 获取命令预览数据
    /// 视口通过此方法获取预览几何，无需知道具体命令类型
    /// @return 预览数据结构体
    virtual CommandPreview preview() const { return {}; }
};

/**
 * @class UndoCommand
 * @brief 可撤销操作基类
 *
 * 表示一个可以被撤销和重做的操作。
 * 命令提交时生成一个 UndoCommand 实例并压入撤销栈。
 *
 * @deprecated 请改用 IUndoRedoCommand (Engine/2D/Edit/IUndoRedoCommand.h)
 *             此类型将在重构阶段 2 完成后移除。
 */
class UndoCommand
{
public:
    explicit UndoCommand(const QString& text);
    virtual ~UndoCommand() = default;

    /// 获取操作描述文本
    QString text() const;

    /// 执行撤销操作
    virtual void undo() = 0;

    /// 执行重做操作
    virtual void redo() = 0;

private:
    QString m_text;
};

/**
 * @class IUndoStack
 * @brief 撤销栈接口
 *
 * 管理可撤销操作的栈，支持 undo/redo 操作。
 *
 * @deprecated 请改用 IUndoRedoManager (Engine/2D/Edit/IUndoRedoManager.h)
 *             此类型将在重构阶段 2 完成后移除。
 */
class IUndoStack
{
public:
    virtual ~IUndoStack() = default;

public:
    /// 将操作压入撤销栈
    /// @param command 可撤销操作
    virtual void push(UndoCommand* command) = 0;

    /// 执行撤销操作
    /// @return 是否成功撤销
    virtual bool undo() = 0;

    /// 执行重做操作
    /// @return 是否成功重做
    virtual bool redo() = 0;

    /// 判断是否可以撤销
    /// @return true 表示可以撤销
    virtual bool canUndo() const = 0;

    /// 判断是否可以重做
    /// @return true 表示可以重做
    virtual bool canRedo() const = 0;

    /// 获取撤销操作描述
    /// @return 撤销操作描述文本
    virtual QString undoText() const = 0;

    /// 获取重做操作描述
    /// @return 重做操作描述文本
    virtual QString redoText() const = 0;

    /// 清空撤销栈
    virtual void clear() = 0;

    /// 获取栈大小
    /// @return 栈中操作数量
    virtual int count() const = 0;
};

/**
 * @class DefaultUndoStack
 * @brief 默认撤销栈实现
 *
 * @deprecated 请改用 UndoRedoManager (Engine/2D/Edit/UndoRedoManager.h)
 *             此类型将在重构阶段 2 完成后移除。
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

private:
    std::vector<std::unique_ptr<UndoCommand>> m_stack;
    int m_currentIndex{ -1 };
};

/**
 * @class PointPickerTool
 * @brief 拾点工具
 *
 * 最基础的交互工具，用于从视口中拾取坐标点。
 * 支持：
 * - 等待第一点
 * - 等待第二点
 * - 支持多个阶段的点收集
 *
 * 可被多个命令复用：画线、画圆、移动等。
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

    const QPointF& lastPoint() const { return m_lastPoint; }
    const std::vector<QPointF>& pickedPoints() const { return m_pickedPoints; }
    bool hasEnoughPoints() const { return m_pickedPoints.size() >= static_cast<size_t>(m_requiredPoints); }

private:
    int m_requiredPoints;
    std::vector<QPointF> m_pickedPoints;
    QPointF m_lastPoint;
    UiServices m_services;
};

/**
 * @class DrawLineCommand
 * @brief 画线命令
 *
 * 交互式命令示例：从视口中拾取两个点，创建一条线段。
 *
 * 命令流程：
 * 1. 激活命令 → 激活拾点工具
 * 2. 用户拾取第一点 → 工具记录第一点，开始预览
 * 3. 用户拾取第二点 → 工具记录第二点，命令完成
 * 4. 命令提交 → 创建线段实体，生成撤销命令，压入撤销栈
 *
 * 使用的工具：PointPickerTool（拾点工具）
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

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(EntityDocument2D* document);

private:
    CommandState m_state{ CommandState::Idle };
    PointPickerTool m_pointPicker;
    const UiServices* m_services{ nullptr };
    EntityDocument2D* m_document{ nullptr };
    QPointF m_previewStart;
    QPointF m_previewEnd;
    QString m_createdEntityId;
};

/**
 * @class SimpleCommandHandler
 * @brief 简单命令处理器实现
 *
 * 非交互式命令的默认实现，激活后立即提交。
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

    void setDocument(EntityDocument2D* document);
    void setSelectedEntityId(const QString& entityId);

private:
    void performBoxSelect();

    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    EntityDocument2D* m_document{ nullptr };
    QString m_selectedEntityId;
    QString m_oldSelectedId;
    bool m_boxSelecting{ false };
    QPointF m_boxSelectStart;
    QPointF m_boxSelectEnd;
};

/**
 * @class MoveUndoCommand
 * @brief 移动命令的撤销操作
 */
class MoveUndoCommand : public UndoCommand
{
public:
    MoveUndoCommand(EntityDocument2D* document,
                    std::map<QString, std::vector<QPointF>> originalPositions);

    void undo() override;
    void redo() override;

private:
    EntityDocument2D* m_document{ nullptr };
    std::map<QString, std::vector<QPointF>> m_originalPositions;
    std::map<QString, std::vector<QPointF>> m_newPositions;
};

class CircleUndoCommand : public UndoCommand
{
public:
    CircleUndoCommand(EntityDocument2D* document, const QString& entityId);
    void undo() override;
    void redo() override;
private:
    EntityDocument2D* m_document{ nullptr };
    QString m_entityId;
    QPointF m_center;
    double m_radius{ 0.0 };
};

class PolylineUndoCommand : public UndoCommand
{
public:
    PolylineUndoCommand(EntityDocument2D* document, const QString& entityId);
    void undo() override;
    void redo() override;
private:
    EntityDocument2D* m_document{ nullptr };
    QString m_entityId;
    QVector<QPointF> m_points;
};

class CopyUndoCommand : public UndoCommand
{
public:
    CopyUndoCommand(EntityDocument2D* document, const QStringList& copiedEntityIds);
    void undo() override;
    void redo() override;
private:
    EntityDocument2D* m_document{ nullptr };
    QStringList m_copiedEntityIds;
    std::vector<std::shared_ptr<UiEntity>> m_copiedEntities;
};

/**
 * @class MoveCommand
 * @brief 移动命令（新架构落地样板）
 *
 * 完整展示命令生命周期：execute → activate → 事件 → isComplete → submit → undo → reset
 *
 * 工作流程：
 * 1. 激活命令 → 检查是否有选中实体，无选中则激活失败
 * 2. 用户点击第一点 → 记录锚点（anchor）
 * 3. 用户移动鼠标 → 更新预览（实时平移）
 * 4. 用户释放鼠标 → 记录目标点，isComplete() → true
 * 5. Dispatcher 自动提交 → commit() 应用平移 → createUndoCommand() 压栈
 *
 * 使用的接口：ITransformable（通过 translate() 方法，不依赖具体实体类型）
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

    void setDocument(EntityDocument2D* document);

private:
    /// 保存选中实体的原始位置（用于撤销）
    void saveOriginalPositions();
    /// 恢复原始位置
    void restoreOriginalPositions();

    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    EntityDocument2D* m_document{ nullptr };
    QPointF m_anchorPoint;       ///< 移动起点
    QPointF m_targetPoint;       ///< 移动终点
    bool m_hasAnchor{ false };   ///< 是否已拾取锚点
    bool m_committed{ false };   ///< 是否已提交（防止重复提交）
    /// 原始位置快照（entityId → original keyPoints）
    std::map<QString, std::vector<QPointF>> m_originalPositions;
};

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

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;

    void setDocument(EntityDocument2D* document);

    const QPointF& rotationCenter() const { return m_rotationCenter; }
    double startAngle() const { return m_startAngle; }
    double currentAngle() const { return m_currentAngle; }
    bool isRotating() const { return m_state == CommandState::Active; }

private:
    void restoreOriginalPoints();

    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    EntityDocument2D* m_document{ nullptr };
    QPointF m_rotationCenter;
    QPointF m_startPoint;
    double m_startAngle{ 0.0 };
    double m_currentAngle{ 0.0 };
    QString m_selectedEntityId;
    std::vector<QPointF> m_originalPoints;
};

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

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(EntityDocument2D* document);

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    EntityDocument2D* m_document{ nullptr };
    QPointF m_center;
    QPointF m_endPoint;
    QString m_createdEntityId;
    bool m_hasCenter{ false };
};

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

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(EntityDocument2D* document);
    void finish();

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    EntityDocument2D* m_document{ nullptr };
    QVector<QPointF> m_points;
    QPointF m_currentPoint;
    bool m_completed{ false };
    QString m_createdEntityId;
};

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

    ITool* activeTool() const override;
    UndoCommand* createUndoCommand() override;
    bool isComplete() const override;
    CommandPreview preview() const override;

    void setDocument(EntityDocument2D* document);

private:
    CommandState m_state{ CommandState::Idle };
    const UiServices* m_services{ nullptr };
    EntityDocument2D* m_document{ nullptr };
    QPointF m_anchorPoint;
    QPointF m_targetPoint;
    bool m_hasAnchor{ false };
    QStringList m_copiedEntityIds;
};

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
