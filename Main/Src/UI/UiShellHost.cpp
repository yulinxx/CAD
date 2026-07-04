#include "UiShellHost.h"

#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "WorkbenchWindow.h"

/// 创建主窗口实例
UiShellHost::UiShellHost()
    : m_mainWindow(std::make_unique<WorkbenchWindow>())
{
}

UiShellHost::~UiShellHost() = default;

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
    if (!m_workbench2D && workbench && workbench->id() == QStringLiteral("2D"))
        m_workbench2D = workbench;
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
    // 先把当前工作台挂到主窗口，再让工作台接管窗口内容
    if (m_workbench)
        m_mainWindow->setWorkbench(m_workbench);

    if (m_workbench)
        m_workbench->attachToWindow(*m_mainWindow);
    // 初始化阶段只做一次挂接，不在这里重复触发切换流程
    // 这里不触发工作台切换，只做初始挂接

    // 初始化状态中心，确保首次打开时状态栏、属性面板有一致的初始值
    if (m_stateCenter && m_workbench)
    {
        m_stateCenter->setCurrentWorkbenchId(m_workbench->id());
        m_stateCenter->setCurrentViewMode(m_workbench->id() == QStringLiteral("3D") ? QStringLiteral("3D Viewport") : QStringLiteral("2D Canvas"));
        m_stateCenter->setCurrentLayerId(QStringLiteral("Default"));
        m_stateCenter->setCurrentDocumentId(QStringLiteral("none"));
        m_stateCenter->setSelectionContext(QStringLiteral("Shell-Init"), QStringLiteral("Ready"));
        // 初始化值只写一次，不在这里做额外回放或二次同步
    }

    // 主题切换由宿主转交给主题服务，再由主窗口统一应用样式
    // 这里仅建立回调，不提前触发任何主题加载
    // 回调里只做主题加载与样式应用，不额外写状态中心
    m_mainWindow->setThemeChangeCallback([this](const QString& themeId) {
        if (m_themeService)
        {
            m_themeService->loadThemeFromId(themeId);
            m_mainWindow->applyTheme(m_themeService->styleSheet());
        }
    });

    // 注册工作台切换工厂，让主窗口按 ID 查找对应工作台实例
    // 3D 工作台在首次请求时惰性创建
    // 工厂返回的工作台与 m_workbench 不同时，同步宿主引用
    m_mainWindow->setWorkbenchFactory([this](const QString& id) -> UiWorkbench* {
        UiWorkbench* target = nullptr;
        if (id == QStringLiteral("3D"))
        {
            if (!m_workbench3D)
            {
                auto wb3d = std::make_unique<Workbench3D>();
                if (!wb3d->initialize(m_services))
                    return m_workbench;
                m_workbench3D = std::move(wb3d);
            }
            target = m_workbench3D.get();
        }
        else if (id == QStringLiteral("2D"))
        {
            target = m_workbench2D ? m_workbench2D : m_workbench;
        }
        else
        {
            target = m_workbench;
        }

        // 宿主与主窗口的工作台引用同步
        if (target && target != m_workbench)
            m_workbench = target;

        return target;
    });

    if (m_stateCenter)
        m_stateCenter->setBusy(false);

    if (m_workbench)
        m_workbench->activate();
    // 激活动作只在初始化尾部做一次，不在宿主层额外补二次激活

    m_mainWindow->show();
}

void UiShellHost::switchWorkbench(const QString& workbenchId)
{
    if (!m_mainWindow)
        return;

    if (m_workbench && m_workbench->id() == workbenchId)
        return;

    UiWorkbench* target = nullptr;
    if (workbenchId == QStringLiteral("3D"))
    {
        if (!m_workbench3D)
        {
            auto wb3d = std::make_unique<Workbench3D>();
            if (!wb3d->initialize(m_services))
                return;
            m_workbench3D = std::move(wb3d);
        }
        target = m_workbench3D.get();
    }
    else if (workbenchId == QStringLiteral("2D"))
    {
        target = m_workbench2D ? m_workbench2D : m_workbench;
    }
    else
    {
        return;
    }

    switchWorkbench(target);
}

void UiShellHost::switchWorkbench(UiWorkbench* workbench)
{
    // 空指针直接返回，避免切换链条中出现无效工作台
    if (!workbench || !m_mainWindow)
        return;

    if (m_workbench == workbench)
        return;

    // 先停用旧工作台，再切换到新工作台
    if (m_workbench)
        m_workbench->deactivate();

    m_workbench = workbench;
    m_mainWindow->setWorkbench(workbench);
    m_mainWindow->triggerWorkbench(workbench->id());
    m_mainWindow->syncWorkbenchStateFromStateCenter();

    // 切换完成后，把状态中心中的工作台 ID 再同步一次，避免外部观察到旧值
    if (m_stateCenter)
        m_stateCenter->setCurrentWorkbenchId(workbench->id());
    // 宿主层只负责切换编排，不在这里再次触发激活或布局恢复
}

/// 获取主窗口指针
WorkbenchWindow* UiShellHost::mainWindow()
{
    return m_mainWindow.get();
}
