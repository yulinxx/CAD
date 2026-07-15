#include "UiCommandDispatcher.h"

#include <QAction>

#include "UiLayoutService.h"
#include "UiServices.h"
#include "UiStateCenter.h"

#include "UI2D/Operation/OperationBus.h"
#include "UI2D/Operation/OperationId.h"

#include "Log/SyLogger.h"

// 将QAction与命令ID绑定，点击时自动执行对应命令
void DefaultUiCommandDispatcher::bindAction(QAction* action, const QString& commandId)
{
    if (!action)
        return;

    QObject::connect(action, &QAction::triggered, action, [this, commandId]() {
        if (m_frameworkServices.canExecuteCommand && !m_frameworkServices.canExecuteCommand(commandId, QStringLiteral("UiCommandDispatcher::bindAction")))
        {
            if (m_frameworkServices.reportError)
                m_frameworkServices.reportError(QStringLiteral("command.denied"), QStringLiteral("Command denied: %1").arg(commandId), QStringLiteral("UiCommandDispatcher::bindAction"));
            return;
        }
        execute(commandId);
        });
}

// 设置当前命令类型，并同步到状态中心
void DefaultUiCommandDispatcher::setCommandType(const QString& commandType)
{
    m_commandType = commandType;
    if (m_stateCenter)
    {
        m_stateCenter->setCurrentCommandType(commandType);
        m_stateCenter->setMetadata({
            { QStringLiteral("commandType"), commandType }
            });
    }
}

// 根据命令ID解析对应的命令类型（2D/3D/View/Edit/File等）
QString DefaultUiCommandDispatcher::resolveCommandType(const QString& commandId) const
{
    if (commandId.startsWith(QStringLiteral("2d.")))
        return QStringLiteral("2D");
    if (commandId.startsWith(QStringLiteral("3d.")))
        return QStringLiteral("3D");
    if (commandId.startsWith(QStringLiteral("view.")))
        return QStringLiteral("View");
    if (commandId.startsWith(QStringLiteral("edit.")))
        return QStringLiteral("Edit");
    if (commandId.startsWith(QStringLiteral("file.")))
        return QStringLiteral("File");
    return QStringLiteral("Other");
}

// 更新命令阶段状态到状态中心
void DefaultUiCommandDispatcher::updatePhase(const QString& phase)
{
    if (!m_stateCenter)
        return;

    m_stateCenter->setCurrentCommandPhase(phase);
    m_stateCenter->setCurrentCommandId(m_activeCommandId);
}

// 命令开始阶段：设置活动命令ID、类型，并同步到状态中心
void DefaultUiCommandDispatcher::begin(const QString& commandId)
{
    m_activeCommandId = commandId;
    m_commandType = resolveCommandType(commandId);

    // 通知工具变化回调
    if (m_toolChangedCallback)
        m_toolChangedCallback(commandId);

    if (!m_stateCenter)
        return;

    // 更新状态中心的命令状态
    m_stateCenter->setBusy(true);
    m_stateCenter->setCurrentCommandId(commandId);
    m_stateCenter->setCurrentCommandOwner(QStringLiteral("dispatcher"));
    m_stateCenter->setCurrentCommandPhase(QStringLiteral("begin"));
    m_stateCenter->setCurrentCommandType(m_commandType);
    m_stateCenter->setMetadata({
        { QStringLiteral("commandState"), QStringLiteral("begin") },
        { QStringLiteral("activeCommandId"), commandId },
        { QStringLiteral("commandOwner"), QStringLiteral("dispatcher") },
        { QStringLiteral("commandPhase"), QStringLiteral("begin") },
        { QStringLiteral("commandType"), m_commandType }
        });
}

namespace
{
    // 将QString命令ID映射为OperationId，用于OperationBus路由
    OperationId mapCommandIdToOperation(const QString& cmd)
    {
        static const struct
        {
            const char* key; OperationId id;
        } map[] = {
{ "2d.select",        OperationId::Tool_Select },
{ "2d.draw_line",     OperationId::Tool_Line },
{ "2d.draw_circle",   OperationId::Tool_Circle },
{ "2d.draw_polyline", OperationId::Tool_Polyline },
{ "2d.draw_arc",      OperationId::Tool_Arc },
{ "2d.draw_polygon",  OperationId::Tool_Polygon },
{ "2d.draw_bezier2",  OperationId::Tool_Bezier2 },
{ "2d.draw_bezier",   OperationId::Tool_Bezier },
{ "2d.draw_nurbs",    OperationId::Tool_Nurbs },
{ "2d.draw_smartline", OperationId::Tool_SmartLine },
{ "2d.move",          OperationId::Edit_Move },
{ "2d.copy",          OperationId::Edit_Copy },
{ "2d.rotate",        OperationId::Edit_Rotate },
{ "2d.mirror",        OperationId::Edit_Mirror },
{ "2d.delete",        OperationId::Edit_Delete },
{ "edit.undo",        OperationId::Edit_Undo },
{ "edit.redo",        OperationId::Edit_Redo },
{ "edit.delete",      OperationId::Edit_Delete },
{ "edit.select_all",  OperationId::Edit_SelectAll },
{ "view.zoom_fit",    OperationId::View_ZoomFit },
{ "view.zoom_in",     OperationId::View_ZoomIn },
{ "view.zoom_out",    OperationId::View_ZoomOut },
        };
        for (const auto& entry : map)
        {
            if (cmd == QLatin1String(entry.key))
                return entry.id;
        }
        return OperationId::None;
    }
} // anonymous namespace

// 执行命令的主入口，优先通过 OperationBus 路由，旧 ICommandHandler 仅作为兜底
void DefaultUiCommandDispatcher::execute(const QString& commandId)
{
    SY_INFOF("[UiCommandDispatcher] execute: command=%s", commandId.toUtf8().constData());

    // 1. 若有活动命令，先取消（不进undo栈）
    if (hasActiveCommand())
    {
        SY_DEBUGF("[UiCommandDispatcher] Canceling active command: %s", m_activeCommandId.toUtf8().constData());
        cancel();
    }

    // 2. 优先尝试通过 OperationBus 路由（新命令系统为主路径）
    auto opId = mapCommandIdToOperation(commandId);
    if (opId != OperationId::None)
    {
        auto* bus = OperationBus::instance();
        if (bus && bus->registry().has(opId))
        {
            SY_DEBUGF("[UiCommandDispatcher] Routing command through OperationBus (primary): %s -> %d",
                commandId.toUtf8().constData(), static_cast<int>(opId));
            begin(commandId);
            OperationResult result = bus->run(opId);
            if (!result.success)
            {
                SY_WARNF("[UiCommandDispatcher] OperationBus execution failed: %s", result.message.toUtf8().constData());
            }
            submit();
            return;
        }
    }

    // 3. 回退到已注册的 ICommandHandler（旧系统兜底）
    auto handler = handlerFor(commandId);
    if (handler)
    {
        SY_DEBUGF("[UiCommandDispatcher] Falling back to ICommandHandler: %s", commandId.toUtf8().constData());

        handler->reset();
        begin(commandId);

        if (handler->activate(m_uiServices))
        {
            SY_INFOF("[UiCommandDispatcher] Command activated: %s, interactive=%s",
                commandId.toUtf8().constData(),
                handler->isInteractive() ? "true" : "false");

            if (!handler->isInteractive())
            {
                SY_DEBUGF("[UiCommandDispatcher] Non-interactive command, submitting immediately: %s", commandId.toUtf8().constData());
                submit();
            }
        }
        else
        {
            SY_ERRORF("[UiCommandDispatcher] error code=command.activate_failed message=Failed to activate command: %s", commandId.toUtf8().constData());
            if (m_frameworkServices.reportError)
                m_frameworkServices.reportError(QStringLiteral("command.activate_failed"), QStringLiteral("Failed to activate command: %1").arg(commandId), QStringLiteral("DefaultUiCommandDispatcher::execute"));
            cancel();
        }
        return;
    }

    // 4. 无handler的命令：仅标记begin → submit完成状态同步
    SY_DEBUGF("[UiCommandDispatcher] No handler found, doing state sync only: %s", commandId.toUtf8().constData());
    begin(commandId);

    if (!m_stateCenter && m_frameworkServices.reportError)
        m_frameworkServices.reportError(QStringLiteral("command.execute_no_state"), QStringLiteral("Command executed without state center: %1").arg(commandId), QStringLiteral("DefaultUiCommandDispatcher::execute"));

    submit();
}

// 提交命令：执行业务提交、创建undo命令、清理状态
void DefaultUiCommandDispatcher::submit()
{
    SY_INFOF("[UiCommandDispatcher] submit: command=%s", m_activeCommandId.toUtf8().constData());

    // submit()是命令生命周期的唯一提交点
    // 1. 调用handler->commit()执行业务提交
    // 2. 创建undo command并压入撤销栈
    // 3. 清理状态中心和handler
    auto handler = currentHandler();
    if (handler)
    {
        // commit()只调用一次，由submit()统一负责
        handler->commit();

        // 只有createUndoCommand()返回非空时才压栈
        // 纯视图命令（Zoom/Pan）返回nullptr，不进栈
        if (m_undoStack)
        {
            if (auto undoCmd = handler->createUndoCommand())
            {
                SY_DEBUGF("[UiCommandDispatcher] Pushing undo command: %s", m_activeCommandId.toUtf8().constData());
                m_undoStack->push(undoCmd);
            }
            else
            {
                SY_DEBUGF("[UiCommandDispatcher] No undo command created: %s", m_activeCommandId.toUtf8().constData());
            }
        }
    }

    updatePhase(QStringLiteral("submit"));

    // 清理状态中心的命令状态
    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("none"));
        m_stateCenter->setMetadata({
            { QStringLiteral("commandState"), QStringLiteral("submit") },
            { QStringLiteral("activeCommandId"), m_activeCommandId },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandPhase"), QStringLiteral("idle") },
            { QStringLiteral("commandType"), QStringLiteral("none") }
            });
    }
    else if (m_frameworkServices.reportError)
    {
        m_frameworkServices.reportError(QStringLiteral("command.submit_no_state"), QStringLiteral("Command submit without state center: %1").arg(m_activeCommandId), QStringLiteral("DefaultUiCommandDispatcher::submit"));
    }

    m_activeCommandId.clear();
    m_commandType.clear();

    if (handler)
    {
        handler->reset();
    }

    SY_DEBUG("[UiCommandDispatcher] Command submit complete");
}

// 取消命令：调用handler的cancel方法并清理状态
void DefaultUiCommandDispatcher::cancel()
{
    SY_INFOF("[UiCommandDispatcher] cancel: command=%s", m_activeCommandId.toUtf8().constData());

    auto handler = currentHandler();
    if (handler)
    {
        SY_DEBUGF("[UiCommandDispatcher] Calling handler cancel: %s", m_activeCommandId.toUtf8().constData());
        handler->cancel();
    }

    // 取消后默认切换回选择工具
    if (m_toolChangedCallback)
        m_toolChangedCallback(QStringLiteral("2d.select"));

    updatePhase(QStringLiteral("cancel"));

    // 清理状态中心的命令状态
    if (m_stateCenter)
    {
        m_stateCenter->setBusy(false);
        m_stateCenter->setCurrentCommandPhase(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandId(QStringLiteral("idle"));
        m_stateCenter->setCurrentCommandOwner(QStringLiteral("none"));
        m_stateCenter->setCurrentCommandType(QStringLiteral("none"));
        m_stateCenter->setMetadata({
            { QStringLiteral("commandState"), QStringLiteral("cancel") },
            { QStringLiteral("activeCommandId"), m_activeCommandId },
            { QStringLiteral("commandOwner"), QStringLiteral("none") },
            { QStringLiteral("commandPhase"), QStringLiteral("idle") },
            { QStringLiteral("commandType"), QStringLiteral("none") }
            });
    }
    else if (m_frameworkServices.reportError)
    {
        m_frameworkServices.reportError(QStringLiteral("command.cancel_no_state"), QStringLiteral("Command cancel without state center: %1").arg(m_activeCommandId), QStringLiteral("DefaultUiCommandDispatcher::cancel"));
    }

    m_activeCommandId.clear();
    m_commandType.clear();

    if (handler)
    {
        handler->reset();
    }

    SY_DEBUG("[UiCommandDispatcher] Command cancel complete");
}

// 设置状态中心引用
void DefaultUiCommandDispatcher::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

// 设置布局服务引用
void DefaultUiCommandDispatcher::setLayoutService(UiLayoutService* layoutService)
{
    m_layoutService = layoutService;
}

// 设置框架服务集合
void DefaultUiCommandDispatcher::setFrameworkServices(const UiFrameworkServices& services)
{
    m_frameworkServices = services;
}

// 获取当前活动命令ID
QString DefaultUiCommandDispatcher::activeCommandId() const
{
    return m_activeCommandId;
}

// 注册命令处理程序
void DefaultUiCommandDispatcher::registerHandler(ICommandHandler* handler)
{
    if (!handler)
        return;
    m_handlers[handler->commandId()] = handler;
}

// 执行撤销操作
bool DefaultUiCommandDispatcher::undo()
{
    SY_INFO("[UiCommandDispatcher] undo");
    if (!m_undoStack)
    {
        SY_WARN("[UiCommandDispatcher] No undo stack available");
        return false;
    }
    bool result = m_undoStack->undo();
    SY_DEBUGF("[UiCommandDispatcher] undo result: %s", result ? "true" : "false");
    return result;
}

// 执行重做操作
bool DefaultUiCommandDispatcher::redo()
{
    SY_INFO("[UiCommandDispatcher] redo");
    if (!m_undoStack)
    {
        SY_WARN("[UiCommandDispatcher] No undo stack available");
        return false;
    }
    bool result = m_undoStack->redo();
    SY_DEBUGF("[UiCommandDispatcher] redo result: %s", result ? "true" : "false");
    return result;
}

// 设置撤销栈引用
void DefaultUiCommandDispatcher::setUndoStack(IUndoStack* undoStack)
{
    m_undoStack = undoStack;
}

// 设置UI服务集合
void DefaultUiCommandDispatcher::setUiServices(const UiServices& services)
{
    m_uiServices = services;
}

// 设置工具变化回调
void DefaultUiCommandDispatcher::setToolChangedCallback(std::function<void(const QString&)> callback)
{
    m_toolChangedCallback = callback;
}

// 转发鼠标按下事件给当前handler
bool DefaultUiCommandDispatcher::forwardMouseDown(int x, int y)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    bool handled = handler->onMouseDown(x, y);

    // 事件转发后检测命令是否完成，若完成则自动提交
    if (handler->isComplete())
    {
        submit();
    }

    return handled;
}

// 转发鼠标移动事件给当前handler
bool DefaultUiCommandDispatcher::forwardMouseMove(int x, int y)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    return handler->onMouseMove(x, y);
}

// 转发鼠标释放事件给当前handler
bool DefaultUiCommandDispatcher::forwardMouseUp(int x, int y)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    bool handled = handler->onMouseUp(x, y);

    // 鼠标释放后检测命令是否完成
    if (handler->isComplete())
    {
        submit();
    }

    return handled;
}

// 转发键盘按键事件给当前handler
bool DefaultUiCommandDispatcher::forwardKeyPress(int key)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    bool handled = handler->onKeyPress(key);

    // 按键后检测命令是否完成（如Enter确认、Esc取消等）
    if (handler->isComplete())
    {
        submit();
    }

    return handled;
}

// 判断是否有活动命令
bool DefaultUiCommandDispatcher::hasActiveCommand() const
{
    return !m_activeCommandId.isEmpty();
}

// 获取当前活动命令的handler
ICommandHandler* DefaultUiCommandDispatcher::currentHandler() const
{
    if (m_activeCommandId.isEmpty())
        return nullptr;

    auto it = m_handlers.find(m_activeCommandId);
    if (it != m_handlers.end())
        return it->second;

    return nullptr;
}

// 根据命令ID查找对应的handler
ICommandHandler* DefaultUiCommandDispatcher::handlerFor(const QString& commandId) const
{
    auto it = m_handlers.find(commandId);
    if (it != m_handlers.end())
        return it->second;

    return nullptr;
}