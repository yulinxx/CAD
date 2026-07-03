/**
 * @file UiShellHost.cpp
 * @brief UI Shell 宿主实现
 */

#include "UiShellHost.h"

#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "WorkbenchWindow.h"

 /// 构造函数，创建主窗口实例
UiShellHost::UiShellHost()
    : m_mainWindow(std::make_unique<WorkbenchWindow>())
{
}

UiShellHost::~UiShellHost() = default;

/// 设置状态中心并传递给主窗口
/// @param stateCenter UI 状态中心
void UiShellHost::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
    m_mainWindow->setUiStateCenter(stateCenter);
}

/// 设置主题服务并传递给主窗口
/// @param themeService 主题服务
void UiShellHost::setThemeService(UiThemeService* themeService)
{
    m_themeService = themeService;
    m_mainWindow->setThemeService(themeService);
}

/// 设置工作台并传递给主窗口
/// @param workbench 工作台实例
void UiShellHost::setWorkbench(UiWorkbench* workbench)
{
    m_workbench = workbench;
    m_mainWindow->setWorkbench(workbench);
}

/// 初始化并显示主窗口
///
/// 执行以下初始化步骤：
/// 1. 附加工作台到主窗口并激活
/// 2. 初始化状态中心默认值
/// 3. 设置主题切换回调
/// 4. 显示主窗口
void UiShellHost::initializeAndShow()
{
    if (m_workbench)
        m_mainWindow->setWorkbench(m_workbench);

    if (m_workbench)
        m_workbench->attachToWindow(*m_mainWindow);

    if (m_stateCenter && m_workbench)
    {
        m_stateCenter->setCurrentWorkbenchId(m_workbench->id());
        m_stateCenter->setCurrentViewMode(m_workbench->id() == QStringLiteral("3D") ? QStringLiteral("3D Viewport") : QStringLiteral("2D Canvas"));
        m_stateCenter->setCurrentLayerId(QStringLiteral("Default"));
        m_stateCenter->setCurrentDocumentId(QStringLiteral("none"));
        m_stateCenter->setSelectionContext(QStringLiteral("Shell-Init"), QStringLiteral("Ready"));
    }

    m_mainWindow->setThemeChangeCallback([this](const QString& themeId) {
        if (m_themeService)
        {
            m_themeService->loadThemeFromId(themeId);
            m_mainWindow->applyTheme(m_themeService->styleSheet());
        }
        });

    if (m_workbench)
        m_workbench->activate();

    m_mainWindow->show();
}

void UiShellHost::switchWorkbench(UiWorkbench* workbench)
{
    if (!workbench || !m_mainWindow)
        return;

    if (m_workbench)
        m_workbench->deactivate();

    m_workbench = workbench;
    m_mainWindow->setWorkbench(workbench);
    m_mainWindow->triggerWorkbench(workbench->id());
    m_mainWindow->syncWorkbenchStateFromStateCenter();

    if (m_stateCenter)
        m_stateCenter->setCurrentWorkbenchId(workbench->id());
}

/// 获取主窗口指针
WorkbenchWindow* UiShellHost::mainWindow()
{
    return m_mainWindow.get();
}