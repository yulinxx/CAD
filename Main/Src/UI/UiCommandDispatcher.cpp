#include "UiCommandDispatcher.h"

#include <QAction>

#include "UiLayoutService.h"
#include "UiServices.h"
#include "UiStateCenter.h"

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

void DefaultUiCommandDispatcher::updatePhase(const QString& phase)
{
    if (!m_stateCenter)
        return;

    m_stateCenter->setCurrentCommandPhase(phase);
    m_stateCenter->setCurrentCommandId(m_activeCommandId);
}

void DefaultUiCommandDispatcher::begin(const QString& commandId)
{
    m_activeCommandId = commandId;
    m_commandType = resolveCommandType(commandId);

    if (m_toolChangedCallback)
        m_toolChangedCallback(commandId);

    if (!m_stateCenter)
        return;

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

void DefaultUiCommandDispatcher::execute(const QString& commandId)
{
    // P0-1: 工具切换时的生命周期顺序
    // 1. 若有活动命令，先取消（不进 undo 栈）
    if (hasActiveCommand())
    {
        cancel();
    }

    // 2. 按 commandId 查找 handler（不依赖 currentHandler）
    auto handler = handlerFor(commandId);
    if (handler)
    {
        // 3. 重置 handler 到 Idle 状态
        handler->reset();

        // 4. begin() 同步状态中心（先于 activate）
        begin(commandId);

        // 5. 激活 handler
        if (handler->activate(m_uiServices))
        {
            // 6. 非交互式命令：直接提交
            if (!handler->isInteractive())
            {
                submit();
            }
            // 交互式命令：保持 Active 状态，等待事件循环中的用户输入
        }
        else
        {
            // 激活失败 → 取消，不进 undo 栈
            if (m_frameworkServices.reportError)
                m_frameworkServices.reportError(QStringLiteral("command.activate_failed"), QStringLiteral("Failed to activate command: %1").arg(commandId), QStringLiteral("DefaultUiCommandDispatcher::execute"));
            cancel();
        }
    }
    else
    {
        // 无 handler 的命令：仅标记 begin → submit 完成状态同步
        begin(commandId);

        if (!m_stateCenter && m_frameworkServices.reportError)
            m_frameworkServices.reportError(QStringLiteral("command.execute_no_state"), QStringLiteral("Command executed without state center: %1").arg(commandId), QStringLiteral("DefaultUiCommandDispatcher::execute"));

        submit();
    }
}

void DefaultUiCommandDispatcher::submit()
{
    // P0-1: submit() 是命令生命周期的唯一提交点
    // 1. 调用 handler->commit() 执行业务提交
    // 2. 创建 undo command 并压入撤销栈
    // 3. 清理状态中心和 handler
    auto handler = currentHandler();
    if (handler)
    {
        // commit() 只调用一次，由 submit() 统一负责
        handler->commit();

        // P0-5: 只有 createUndoCommand() 返回非空时才压栈
        // 纯视图命令（Zoom/Pan）返回 nullptr，不进栈
        if (m_undoStack)
        {
            if (auto undoCmd = handler->createUndoCommand())
            {
                m_undoStack->push(undoCmd);
            }
        }
    }

    updatePhase(QStringLiteral("submit"));

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
}

void DefaultUiCommandDispatcher::cancel()
{
    auto handler = currentHandler();
    if (handler)
    {
        handler->cancel();
    }

    if (m_toolChangedCallback)
        m_toolChangedCallback(QStringLiteral("2d.select"));

    updatePhase(QStringLiteral("cancel"));

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
}

void DefaultUiCommandDispatcher::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

void DefaultUiCommandDispatcher::setLayoutService(UiLayoutService* layoutService)
{
    m_layoutService = layoutService;
}

void DefaultUiCommandDispatcher::setFrameworkServices(const UiFrameworkServices& services)
{
    m_frameworkServices = services;
}

QString DefaultUiCommandDispatcher::activeCommandId() const
{
    return m_activeCommandId;
}

void DefaultUiCommandDispatcher::registerHandler(ICommandHandler* handler)
{
    if (!handler)
        return;
    m_handlers[handler->commandId()] = handler;
}

bool DefaultUiCommandDispatcher::undo()
{
    if (!m_undoStack)
        return false;
    return m_undoStack->undo();
}

bool DefaultUiCommandDispatcher::redo()
{
    if (!m_undoStack)
        return false;
    return m_undoStack->redo();
}

void DefaultUiCommandDispatcher::setUndoStack(IUndoStack* undoStack)
{
    m_undoStack = undoStack;
}

void DefaultUiCommandDispatcher::setUiServices(const UiServices& services)
{
    m_uiServices = services;
}

void DefaultUiCommandDispatcher::setToolChangedCallback(std::function<void(const QString&)> callback)
{
    m_toolChangedCallback = callback;
}

bool DefaultUiCommandDispatcher::forwardMouseDown(int x, int y)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    bool handled = handler->onMouseDown(x, y);

    // P0-1: 事件转发后检测命令是否完成，若完成则自动提交
    if (handler->isComplete())
    {
        submit();
    }

    return handled;
}

bool DefaultUiCommandDispatcher::forwardMouseMove(int x, int y)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    return handler->onMouseMove(x, y);
}

bool DefaultUiCommandDispatcher::forwardMouseUp(int x, int y)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    bool handled = handler->onMouseUp(x, y);

    // P0-1: 鼠标释放后检测命令是否完成
    if (handler->isComplete())
    {
        submit();
    }

    return handled;
}

bool DefaultUiCommandDispatcher::forwardKeyPress(int key)
{
    auto handler = currentHandler();
    if (!handler)
        return false;

    bool handled = handler->onKeyPress(key);

    // P0-1: 按键后检测命令是否完成（如 Enter 确认、Esc 取消等）
    if (handler->isComplete())
    {
        submit();
    }

    return handled;
}

bool DefaultUiCommandDispatcher::hasActiveCommand() const
{
    return !m_activeCommandId.isEmpty();
}

ICommandHandler* DefaultUiCommandDispatcher::currentHandler() const
{
    if (m_activeCommandId.isEmpty())
        return nullptr;

    auto it = m_handlers.find(m_activeCommandId);
    if (it != m_handlers.end())
        return it->second;

    return nullptr;
}

ICommandHandler* DefaultUiCommandDispatcher::handlerFor(const QString& commandId) const
{
    auto it = m_handlers.find(commandId);
    if (it != m_handlers.end())
        return it->second;

    return nullptr;
}
