#include "UiShellHost.h"

#include "UiCommandDispatcher.h"
#include "UiCommandHandler.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "WorkbenchWindow.h"

/// 创建主窗口实例
UiShellHost::UiShellHost()
    : m_mainWindow(std::make_unique<WorkbenchWindow>())
{
}

UiShellHost::~UiShellHost()
{
    shutdown();
}

/// 设置状态中心并传递给主窗口
/// @param stateCenter UI 状态中心
void UiShellHost::setStateCenter(UiStateCenter* stateCenter)
{
    // 宿主只转发状态中心引用，不在这里做额外初始化
    m_stateCenter = stateCenter;
    m_mainWindow->setUiStateCenter(stateCenter);
}

/// 设置主题服务并传递给主窗口
/// @param themeService 主题服务
void UiShellHost::setThemeService(UiThemeService* themeService)
{
    // 宿主只转发主题服务引用，不在这里提前加载主题
    m_themeService = themeService;
    m_mainWindow->setThemeService(themeService);
}

/// 设置命令分发器
/// @param dispatcher 命令分发器
void UiShellHost::setCommandDispatcher(UiCommandDispatcher* dispatcher)
{
    m_commandDispatcher = dispatcher;
    m_mainWindow->setCommandDispatcher(dispatcher);
}

/// 设置撤销栈
/// @param undoStack 撤销栈
void UiShellHost::setUndoStack(IUndoStack* undoStack)
{
    m_undoStack = undoStack;
    m_mainWindow->setUndoStack(undoStack);
}

/// 设置 UI 服务集合
/// @param services UI 服务集合
void UiShellHost::setUiServices(const UiServices& services)
{
    m_services = services;
}

/// 设置工作台并传递给主窗口
/// @param workbench 工作台实例
void UiShellHost::setWorkbench(UiWorkbench* workbench)
{
    // 宿主只转发工作台引用，不在这里触发切换流程
    m_workbench = workbench;
    m_mainWindow->setWorkbench(workbench);
}

/// 初始化并显示主窗口
///
/// 执行以下初始化步骤：
/// 1. 附加工作台到主窗口并激活
/// 2. 初始化状态中心默认值
/// 3. 设置主题切换回调
/// 4. 注册工作台切换工厂
/// 5. 显示主窗口
void UiShellHost::initializeAndShow()
{
    if (!m_mainWindow || !m_workbench)
        return;

    m_workbench->attachToWindow(*m_mainWindow);
    m_workbench->activate();

    m_mainWindow->setThemeChangeCallback([this](const QString& themeId) {
        if (m_themeService)
        {
            m_themeService->loadThemeFromId(themeId);
            m_mainWindow->applyTheme(m_themeService->styleSheet());
        }
    });

    m_mainWindow->setWorkbenchFactory([this](const QString& id) -> UiWorkbench* {
        return resolveWorkbench(id);
    });

    if (m_stateCenter)
        m_stateCenter->setBusy(false);

    m_mainWindow->show();
}

UiWorkbench* UiShellHost::resolveWorkbench(const QString& workbenchId)
{
    // 2D 工作台由外部注入，直接返回
    if (workbenchId == QStringLiteral("2D"))
        return m_workbench;

    // 3D 工作台惰性创建（首次访问时初始化）
    if (workbenchId == QStringLiteral("3D"))
    {
        if (!m_workbench3D)
        {
            auto wb3d = std::make_unique<Workbench3D>();
            if (!wb3d->initialize(m_services))
                return m_workbench; // 初始化失败时回退到 2D
            m_workbench3D = std::move(wb3d);
        }
        return m_workbench3D.get();
    }

    // 未知工作台 ID 回退到 2D
    return m_workbench;
}

void UiShellHost::switchWorkbench(const QString& workbenchId)
{
    UiWorkbench* target = resolveWorkbench(workbenchId);
    if (target)
        switchWorkbench(target);
}

void UiShellHost::switchWorkbench(UiWorkbench* workbench)
{
    if (!workbench || !m_mainWindow)
        return;

    if (m_workbench == workbench)
        return;

    if (m_workbench)
        m_workbench->deactivate();

    m_workbench = workbench;
    m_mainWindow->setWorkbench(workbench);
    m_mainWindow->triggerWorkbench(workbench->id());
    m_mainWindow->syncWorkbenchStateFromStateCenter();
}

void UiShellHost::shutdown()
{
    if (m_workbench)
    {
        m_workbench->shutdown();
        m_workbench = nullptr;
    }
    m_workbench3D.reset();
    m_mainWindow.reset();
    m_services = UiServices{};
}

/// 获取主窗口指针
WorkbenchWindow* UiShellHost::mainWindow()
{
    return m_mainWindow.get();
}
