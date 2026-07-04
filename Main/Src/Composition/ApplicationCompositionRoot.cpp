#include "ApplicationCompositionRoot.h"

#include "../UI/UiCommandDispatcher.h"
#include "../UI/UiLayoutService.h"
#include "../UI/UiShellHost.h"
#include "../UI/UiStateCenter.h"
#include "../UI/UiThemeService.h"

ApplicationCompositionRoot::ApplicationCompositionRoot()
    : m_stateCenter(std::make_unique<UiStateCenter>())
    , m_themeService(std::make_unique<DefaultUiThemeService>())
    , m_layoutService(std::make_unique<DefaultUiLayoutService>())
    , m_commandDispatcher(std::make_unique<DefaultUiCommandDispatcher>())
    , m_shellHost(std::make_unique<UiShellHost>())
{
    m_commandDispatcher->setStateCenter(m_stateCenter.get());
    m_commandDispatcher->setLayoutService(m_layoutService.get());
    m_shellHost->setStateCenter(m_stateCenter.get());
    m_shellHost->setThemeService(m_themeService.get());
}

UiShellHost* ApplicationCompositionRoot::shellHost(){ return m_shellHost.get(); }
UiStateCenter* ApplicationCompositionRoot::stateCenter(){ return m_stateCenter.get(); }
UiThemeService* ApplicationCompositionRoot::themeService(){ return m_themeService.get(); }
UiLayoutService* ApplicationCompositionRoot::layoutService(){ return m_layoutService.get(); }
UiCommandDispatcher* ApplicationCompositionRoot::commandDispatcher(){ return m_commandDispatcher.get(); }