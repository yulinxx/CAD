#include "ApplicationCompositionRoot.h"

#include "../UI/UiCommandDispatcher.h"
#include "../UI/UiCommandHandler.h"
#include "../UI/UiLayoutService.h"
#include "../UI/UiServices.h"
#include "../UI/UiShellHost.h"
#include "../UI/UiStateCenter.h"
#include "../UI/UiThemeService.h"
#include "../UI/CreateCommands.h"
#include "../UI/TransformCommands.h"
#include "../UI/SelectCommands.h"
#include "../UI/CommandHandlerAdapter.h"

#include "UI2D/Operation/OperationId.h"
#include "Log/SyLogger.h"

// 应用组合根组件，负责创建和组装所有核心服务
// 作为依赖注入的中心点，管理UI层和命令系统的生命周期
ApplicationCompositionRoot::ApplicationCompositionRoot()
    : m_stateCenter(std::make_unique<UiStateCenter>())
    , m_themeService(std::make_unique<DefaultUiThemeService>())
    , m_layoutService(std::make_unique<DefaultUiLayoutService>())
    , m_commandDispatcher(std::make_unique<DefaultUiCommandDispatcher>())
    , m_undoStack(std::make_unique<DefaultUndoStack>())
    , m_shellHost(std::make_unique<UiShellHost>())
    , m_document2D(std::make_unique<SceneDocument2D>())
    , m_operationBus(std::make_unique<OperationBus>())
{
    // 配置命令分发器的核心依赖
    m_commandDispatcher->setStateCenter(m_stateCenter.get());
    m_commandDispatcher->setLayoutService(m_layoutService.get());
    m_commandDispatcher->setUndoStack(m_undoStack.get());

    // 组装UI服务集合，统一注入到命令分发器
    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.commandDispatcher = m_commandDispatcher.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.undoStack = m_undoStack.get();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();
    m_commandDispatcher->setUiServices(uiServices);

    // 配置UI壳宿主的核心依赖
    m_shellHost->setStateCenter(m_stateCenter.get());
    m_shellHost->setThemeService(m_themeService.get());
    m_shellHost->setCommandDispatcher(m_commandDispatcher.get());
    m_shellHost->setUndoStack(m_undoStack.get());

    // 注册所有命令处理器
    registerCommands();

    // 将 OperationBus 设为活动实例，供无上下文调用方使用
    OperationBus::setActiveInstance(m_operationBus.get());

    // 通过适配器将旧命令注册到 OperationBus
    registerCommandAdapters();

    SY_INFO("[ApplicationCompositionRoot] initialized with OperationBus + CommandHandlerAdapters");
}

// 注册所有命令处理器到命令分发器
// 命令处理器按功能分类：选择命令、创建命令（绘制）、变换命令（编辑）
void ApplicationCompositionRoot::registerCommands()
{
    // 选择命令
    m_commandHandlers.push_back(std::make_unique<SelectCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 创建命令 - 绘制工具
    m_commandHandlers.push_back(std::make_unique<DrawLineCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<CircleCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<PolylineCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 阶段 1：补全基础绘图工具
    m_commandHandlers.push_back(std::make_unique<ArcCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<PolygonCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 变换命令 - 编辑操作
    m_commandHandlers.push_back(std::make_unique<MoveCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<RotateCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<CopyCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    // 阶段 2：补全编辑操作
    m_commandHandlers.push_back(std::make_unique<DeleteCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<MirrorCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());
}

UiShellHost* ApplicationCompositionRoot::shellHost()
{
    return m_shellHost.get();
}
UiStateCenter* ApplicationCompositionRoot::stateCenter()
{
    return m_stateCenter.get();
}
UiThemeService* ApplicationCompositionRoot::themeService()
{
    return m_themeService.get();
}
UiLayoutService* ApplicationCompositionRoot::layoutService()
{
    return m_layoutService.get();
}
UiCommandDispatcher* ApplicationCompositionRoot::commandDispatcher()
{
    return m_commandDispatcher.get();
}
IInteractionDispatcher* ApplicationCompositionRoot::interactionDispatcher()
{
    return dynamic_cast<IInteractionDispatcher*>(m_commandDispatcher.get());
}
IUndoStack* ApplicationCompositionRoot::undoStack()
{
    return m_undoStack.get();
}

OperationBus* ApplicationCompositionRoot::operationBus()
{
    return m_operationBus.get();
}

// 为所有旧式 ICommandHandler 创建适配器并注册到 OperationBus
void ApplicationCompositionRoot::registerCommandAdapters()
{
    if (!m_operationBus)
        return;

    // 构建 UiServices（与构造函数保持一致）
    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.commandDispatcher = m_commandDispatcher.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.undoStack = m_undoStack.get();
    uiServices.operationBus = m_operationBus.get();
    uiServices.document2D = m_document2D.get();

    int count = 0;
    for (const auto& handler : m_commandHandlers)
    {
        auto opId = CommandHandlerAdapter::mapCommandId(handler->commandId());
        if (opId == OperationId::None)
        {
            SY_DEBUGF("[ApplicationCompositionRoot] No OperationId mapping for handler: %s",
                handler->commandId().toUtf8().constData());
            continue;
        }

        auto adapter = std::make_unique<CommandHandlerAdapter>(handler.get(), uiServices);
        m_operationBus->registry().registerOperation(std::move(adapter));
        ++count;
    }

    SY_INFOF("[ApplicationCompositionRoot] Registered %d CommandHandlerAdapters on OperationBus", count);
}