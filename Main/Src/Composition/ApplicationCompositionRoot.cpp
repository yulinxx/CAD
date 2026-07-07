#include "ApplicationCompositionRoot.h"

#include "../UI/UiCommandDispatcher.h"
#include "../UI/UiCommandHandler.h"
#include "../UI/UiLayoutService.h"
#include "../UI/UiServices.h"
#include "../UI/UiShellHost.h"
#include "../UI/UiStateCenter.h"
#include "../UI/UiThemeService.h"

ApplicationCompositionRoot::ApplicationCompositionRoot()
    : m_stateCenter(std::make_unique<UiStateCenter>())
    , m_themeService(std::make_unique<DefaultUiThemeService>())
    , m_layoutService(std::make_unique<DefaultUiLayoutService>())
    , m_commandDispatcher(std::make_unique<DefaultUiCommandDispatcher>())
    , m_undoStack(std::make_unique<DefaultUndoStack>())
    , m_shellHost(std::make_unique<UiShellHost>())
{
    m_commandDispatcher->setStateCenter(m_stateCenter.get());
    m_commandDispatcher->setLayoutService(m_layoutService.get());
    m_commandDispatcher->setUndoStack(m_undoStack.get());

    UiServices uiServices;
    uiServices.stateCenter = m_stateCenter.get();
    uiServices.themeService = m_themeService.get();
    uiServices.layoutService = m_layoutService.get();
    uiServices.commandDispatcher = m_commandDispatcher.get();
    uiServices.interactionDispatcher = interactionDispatcher();
    uiServices.undoStack = m_undoStack.get();
    m_commandDispatcher->setUiServices(uiServices);

    m_shellHost->setStateCenter(m_stateCenter.get());
    m_shellHost->setThemeService(m_themeService.get());
    m_shellHost->setCommandDispatcher(m_commandDispatcher.get());
    m_shellHost->setUndoStack(m_undoStack.get());

    registerCommands();
}

void ApplicationCompositionRoot::registerCommands()
{
    m_commandHandlers.push_back(std::make_unique<DrawLineCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<SelectCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<RotateCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<MoveCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<CircleCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<PolylineCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());

    m_commandHandlers.push_back(std::make_unique<CopyCommand>());
    m_commandDispatcher->registerHandler(m_commandHandlers.back().get());
}

UiShellHost* ApplicationCompositionRoot::shellHost(){ return m_shellHost.get(); }
UiStateCenter* ApplicationCompositionRoot::stateCenter(){ return m_stateCenter.get(); }
UiThemeService* ApplicationCompositionRoot::themeService(){ return m_themeService.get(); }
UiLayoutService* ApplicationCompositionRoot::layoutService(){ return m_layoutService.get(); }
UiCommandDispatcher* ApplicationCompositionRoot::commandDispatcher(){ return m_commandDispatcher.get(); }
IInteractionDispatcher* ApplicationCompositionRoot::interactionDispatcher(){ return dynamic_cast<IInteractionDispatcher*>(m_commandDispatcher.get()); }
IUndoStack* ApplicationCompositionRoot::undoStack(){ return m_undoStack.get(); }