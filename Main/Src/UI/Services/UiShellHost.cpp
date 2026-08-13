#include "UiShellHost.h"

#include "Log/SyLogger.h"
#include "UiStateCenter.h"
#include "UiThemeService.h"
#include "UiWorkbench.h"
#include "WorkbenchWindow.h"
#include "UI2D/Operation/OperationBus.h"

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

/// 设置操作总线
/// @param bus 操作总线
void UiShellHost::setOperationBus(OperationBus* bus)
{
    m_operationBus = bus;
    m_mainWindow->setOperationBus(bus);
}

/// 设置 UI 服务集合
/// @param services UI 服务集合
void UiShellHost::setUiServices(const UiServices& services)
{
    m_services = services;
    if (m_mainWindow)
    {
        m_mainWindow->setUiServices(services);
    }
}

void UiShellHost::setFrameworkServices(const UiFrameworkServices& services)
{
    if (m_mainWindow)
    {
        m_mainWindow->setFrameworkServices(services);
    }
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
    {
        SY_ERROR("[UiShellHost] error code=shell.init_failed message=initializeAndShow called without main window or "
                 "workbench");
        return;
    }

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
    {
        m_stateCenter->setBusy(false);
    }

    m_mainWindow->show();
}

UiWorkbench* UiShellHost::resolveWorkbench(const QString& workbenchId)
{
    // 2D 工作台由外部注入，直接返回
    if (workbenchId == QStringLiteral("2D"))
    {
        return m_workbench;
    }

#if BUILD_UI3D
    if (workbenchId == QStringLiteral("3D"))
    {
        if (!m_workbench3D)
        {
            auto wb3d = std::make_unique<Workbench3D>();
            if (!wb3d->initialize(m_services))
            {
                SY_WARN("Workbench3D initialization failed, falling back to 2D");
                return m_workbench;
            }
            m_workbench3D = std::move(wb3d);
        }
        return m_workbench3D.get();
    }
#endif

    SY_WARNF("Unknown workbench id '%s', falling back to 2D", workbenchId.toUtf8().constData());
    return m_workbench;
}

void UiShellHost::switchWorkbench(const QString& workbenchId)
{
    UiWorkbench* target = resolveWorkbench(workbenchId);
    if (target)
    {
        switchWorkbench(target);
    }
}

void UiShellHost::switchWorkbench(UiWorkbench* workbench)
{
    if (!workbench || !m_mainWindow)
    {
        return;
    }

    if (m_workbench == workbench)
    {
        return;
    }

    // 更新宿主层的工作台引用
    m_workbench = workbench;

    // 统一走 triggerWorkbench 处理完整的切换生命周期:
    //   保存旧状态 → 停用旧工作台 → 清理 UI → 创建新工作台 → 附加 → 激活
    // 不在这里单独调用 deactivate() 或 setWorkbench()，
    // 避免 triggerWorkbench 内部看到的是已被替换的新工作台
    m_mainWindow->triggerWorkbench(workbench->id());
    m_mainWindow->syncWorkbenchStateFromStateCenter();
}

void UiShellHost::shutdown()
{
    // 3D 工作台惰性创建，UiShellHost::m_workbench 在 triggerWorkbench 路径下
    // 不会更新为 3D 实例。若用户从 3D 模式直接退出，m_workbench 仍指向 2D，
    // 上面的 shutdown() 只会关闭 2D，3D 的 deactivate() 永远不会被调用，
    // 导致 RenderWidget3D 信号未断开、服务在析构中被访问 → 堆损坏。
    // 修复：显式对 3D 工作台调用 shutdown（与当前 m_workbench 不同时）
    if (m_workbench3D && m_workbench3D.get() != m_workbench)
    {
        m_workbench3D->shutdown();
    }

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