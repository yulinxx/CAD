#include "Assembly/Runtime/ShellView.h"

#include <utility>

#include <QApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QMainWindow>
#include <QMenuBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "Assembly/Commands/CommandActions.h"
#include "Assembly/Host/TitleBarHostAdapter.h"
#include "Assembly/Menus/MenuController.h"
#include "Assembly/Widgets/RegionContainer.h"

#include "UiShell/Contract/IShellHost.h"
#include "Platform/WindowFrame.h"
#include "UiShell/Titlebar/ITitleBarMenuHost.h"
#include "UiShell/Titlebar/ITitleBarView.h"
#include "UiShell/i18n/ITranslationService.h"

namespace RD
{
    ShellView::ShellView(const ShellServices& services,
        const AssemblyInfo& info,
        MenuController& menuController,
        CommandActions& commandActions,
        IShellHost* host,
        std::function<void(const QString&)> reportMessage)
        : m_shellServices(services)
        , m_assemblyInfo(info)
        , m_menuCtrl(menuController)
        , m_cmdActions(commandActions)
        , m_host(host)
        , m_reportMsgFn(std::move(reportMessage))
        , m_titleBarHostAdapter(std::make_unique<TitleBarHostAdapter>())
    {
    }

    ShellView::~ShellView()
    {
        if (m_titleBarHostAdapter)
            m_titleBarHostAdapter->bind(nullptr, nullptr, nullptr);

        delete m_shell.data();
        m_shell = nullptr;
        m_titleBarWidget = nullptr;
        m_titleBarView = nullptr;
        m_titleBarMenuHost = nullptr;
    }

    QWidget* ShellView::shell() const
    {
        return m_shell.data();
    }

    QWidget* ShellView::takeShell()
    {
        if (m_titleBarHostAdapter)
            m_titleBarHostAdapter->bind(nullptr, nullptr, nullptr);

        if (m_frame)
        {
            m_frame->setResizeBorderWidget(nullptr);
            m_frame.reset();
        }

        m_titleBarWidget = nullptr;
        m_titleBarView = nullptr;
        m_titleBarMenuHost = nullptr;

        QWidget* shell = m_shell.data();
        m_shell = nullptr;
        return shell;
    }

    bool ShellView::attachWindow(QMainWindow* window)
    {
        if (!window || !m_shell)
            return false;

        const QSize minimumSize(m_assemblyInfo.nMinWidth, m_assemblyInfo.nMinHeight);
        if (m_assemblyInfo.bNativeTitleBar)
        {
            window->setMinimumSize(minimumSize);
        }
        else
        {
            m_frame = createWindowFrame();
            m_frame->attach(window);
            m_frame->setMinimumSize(minimumSize);
        }

        m_window = window;

        const int nBorder = m_frame ? m_frame->resizeBorderWidth() : 0;
        QWidget* container = new QWidget(window);
        container->setObjectName(QStringLiteral("msui.window.container"));

        auto* layout = new QVBoxLayout(container);
        layout->setContentsMargins(nBorder, nBorder, nBorder, nBorder);
        layout->setSpacing(0);

        // shell 仍由 ShellView 持有,切换语言时会在原容器里替换成新 shell。
        m_container = container;
        layout->addWidget(m_shell.data());
        window->setCentralWidget(container);

        if (m_frame)
        {
            m_frame->setResizeBorderWidget(container);
            bindTitleBarToWindow();

            if (m_titleBarWidget)
            {
                QTimer::singleShot(0, m_titleBarHostAdapter.get(), [this]() {
                    if (m_frame)
                        m_frame->refreshTargetHwnds();
                    });
            }
        }
        else
        {
            // 原生标题栏模式
            bindTitleBarToWindow();
        }

        // 窗口与任务栏图标直接取 profile 显式指定的 titleBar.icon，不做扩展名探测；
        const QString strIconPath = m_assemblyInfo.titleBar.strIcon.trimmed();
        if (!strIconPath.isEmpty())
        {
            const QIcon appIcon(strIconPath);
            if (!appIcon.isNull())
            {
                QApplication::setWindowIcon(appIcon);
                window->setWindowIcon(appIcon);
            }
        }
        return true;
    }

    IRegionContainer* ShellView::regionContainer(const QString& regionId) const
    {
        for (RegionContainer* host : m_regContainers)
        {
            if (host->regionId() == regionId)
                return host;
        }
        return nullptr;
    }

    IRegionContainer* ShellView::regionContainerForPanel(const QString& panelId) const
    {
        // 面板只记录在所属区域内，因此需要逐个区域反查。
        for (RegionContainer* host : m_regContainers)
        {
            if (host->isPanelMounted(panelId))
                return host;
        }

        return nullptr;
    }

    void ShellView::rebuild()
    {
        {
            m_assemblyDiagnostics.clear();
            if (m_titleBarHostAdapter)
                m_titleBarHostAdapter->bind(nullptr, nullptr, nullptr);

            if (m_shell && m_container && m_container->layout())
                m_container->layout()->removeWidget(m_shell.data());

            delete m_shell.data();

            m_shell = nullptr;
            m_regContainers.clear();
            m_titleBarWidget = nullptr;
            m_titleBarView = nullptr;
            m_titleBarMenuHost = nullptr;
        }

        m_shell = new QWidget;
        m_shell->setObjectName(QStringLiteral("msui.shell"));

        // 命令动作挂在新 shell 下：菜单、工具栏与面板在下面创建时按需取用；旧动作已随旧 shell 销毁。
        m_cmdActions.attach(m_shell.data());

        auto* outer = new QVBoxLayout(m_shell);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->setSpacing(4);

        // 区域按上/中/下三组放置：每组内部沿用计划顺序，保证同样的 profile 得到同样的界面。
        for (const RegionInfo& region : m_assemblyInfo.regions)
        {
            if (isTopGroup(region.type))
            {
                outer->addWidget(createRegionContainer(region, m_shell));
            }
        }

        bool bHasMiddle = false;
        for (const RegionInfo& region : m_assemblyInfo.regions)
        {
            if (isMiddleGroup(region.type))
            {
                bHasMiddle = true;
                break;
            }
        }

        if (bHasMiddle)
        {
            auto* middle = new QHBoxLayout;
            middle->setContentsMargins(0, 0, 0, 0);
            middle->setSpacing(4);

            for (const RegionInfo& region : m_assemblyInfo.regions)
            {
                if (!isMiddleGroup(region.type))
                    continue;

                // 左右停靠区保持自身宽度，剩余的给中间区域。
                const int stretch = (region.type == RegionType::CenterView) ? 1 : 0;
                middle->addWidget(createRegionContainer(region, m_shell), stretch);
            }

            outer->addLayout(middle, 1);
        }

        for (const RegionInfo& region : m_assemblyInfo.regions)
        {
            if (isBottomGroup(region.type))
                outer->addWidget(createRegionContainer(region, m_shell));
        }

        // 所有区域就绪后再合并菜单：此时标题栏与菜单栏都已存在，
        // 菜单内嵌特性随后才能被识别。
        syncMenuPlacement();

        // 窗口已存在时把新 shell 放回原容器：语言切换即原地刷新，无需重启。
        if (m_container && m_container->layout())
            m_container->layout()->addWidget(m_shell.data());

        // 新标题栏替换了旧控件：窗口状态监听与宿主指针必须在同一定点重绑，
        // 否则开关变化触发的 rebuild 会留下指向已删除标题栏的适配器。
        bindTitleBarToWindow();

        // 新控件创建完毕后再推上下文：此时属性/语言均已确定，
        // 需要皮肤或语言的控件（QML 等）拿到的是最终值。
        notifyHostContext();
    }

    RegionContainer* ShellView::createRegionContainer(const RegionInfo& region, QWidget* parent)
    {
        auto* container = new RegionContainer(parent);
        container->apply(region);
        m_regContainers.append(container);

        for (const PanelInfo& panelInfo : region.panels)
        {
            // 运行期只认工厂 id，面板类型与外观完全由业务侧工厂决定。
            IWidgetFactory* factory = findFactory(panelInfo.factory);

            if (!factory)
            {
                const QString message =
                    QStringLiteral("no factory '%1' registered for panel '%2'").arg(panelInfo.factory, panelInfo.strId);

                if (m_reportMsgFn)
                    m_reportMsgFn(message);

                // 与 props 校验失败对齐：工厂缺失也进入装配期诊断，
                // 宿主能通过 diagnostics() 看到所有缺失面板，而不只是最后一条
                Diagnostic d;
                d.level = Diagnostic::Level::Error;
                d.strJsonPath = QStringLiteral("$.panels[%1].factory").arg(panelInfo.strId);
                d.strMsg = message;
                m_assemblyDiagnostics.append(std::move(d));
                continue;
            }

            PanelCreateRequest info;
            info.strPanelId = panelInfo.strId;
            info.strFactoryId = panelInfo.factory;
            // profile 里的工厂私有参数原样透传，客户面板按自己的约定解释
            info.props = panelInfo.props;
            // 宿主上下文按快照传给工厂：工厂不需要（也不允许）读全局状态
            info.context = hostContext();
            info.host = m_host;
            info.parent = container;

            // 面板创建前的 props 校验：工厂可覆盖 validateProps() 强约束字段
            const PropsValidation v = factory->validateProps(panelInfo.props);
            if (!v.bValid)
            {
                if (m_reportMsgFn)
                {
                    m_reportMsgFn(QStringLiteral("props validation failed for panel '%1' (factory '%2'): %3")
                        .arg(panelInfo.strId, panelInfo.factory, v.strMsg));
                }

                Diagnostic d;
                d.level = Diagnostic::Level::Error;
                d.strJsonPath = QStringLiteral("$.panels[%1].props").arg(panelInfo.strId);
                d.strMsg = QStringLiteral("props validation failed: %1").arg(v.strMsg);
                m_assemblyDiagnostics.append(std::move(d));
                continue;  // skip 这个面板，不创建控件
            }

            if (QWidget* widget = factory->create(info))
                container->mountPanel(panelInfo, widget);
        }

        if (region.type == RegionType::CenterView && m_shellServices.centerView)
        {
            if (QWidget* widget = m_shellServices.centerView->createCenterView(container))
                container->setCenterWidget(widget);
        }

        if (region.type == RegionType::TitleBar)
            createTitleBarInto(container);

        if (region.type == RegionType::MenuBar)
        {
            if (QMenuBar* menuBar = createMenuBar(container))
                container->setCenterWidget(menuBar);
        }

        // TitleBar / MenuBar / CenterView 有特殊的 center widget 赋值；
        // 其余普通区域统一由上方的 region.panels 循环处理面板挂载。

        return container;
    }

    IWidgetFactory* ShellView::findFactory(const QString& factoryId)
    {
        if (m_shellServices.widgetReg)
        {
            if (IWidgetFactory* factory = m_shellServices.widgetReg->find(factoryId))
                return factory;
        }
        if (factoryId == QLatin1String(kToolBarFactoryId))
            return &m_toolBarFactory;
        return nullptr;
    }

    void ShellView::createTitleBarInto(RegionContainer* host)
    {
        if (!host)
            return;

        // 先清空上一份标题栏的引用：重建时旧控件已被销毁，不能继续持有。
        m_titleBarWidget = nullptr;
        m_titleBarView = nullptr;
        m_titleBarMenuHost = nullptr;

        if (!m_assemblyInfo.titleBar.bPresent)
            return;

        TitleBarSpec spec;
        spec.bEnabled = true;
        // 计划阶段已优先按 strTitleKey 从语言包解析出 title；titleKey 一并传入，
        // 客户标题栏可在自己实现里重新解析（例如运行期换语言）。
        spec.strTitle = m_assemblyInfo.titleBar.strTitle;
        spec.strTitleKey = m_assemblyInfo.titleBar.strTitleKey;
        spec.strIcon = m_assemblyInfo.titleBar.strIcon;
        spec.listButtons = m_assemblyInfo.titleBar.buttons;
        spec.nHeight = m_assemblyInfo.titleBar.nHeight;
        spec.bShowTitle = m_assemblyInfo.titleBar.bShowTitle;
        spec.strBkgColor = m_assemblyInfo.titleBar.strBkgColor;
        spec.strTextColor = m_assemblyInfo.titleBar.strTextColor;

        // 业务侧提供标题栏时用其实现，否则用框架自带的 createTitleBar 兜底。
        // createTitleBar 返回 ITitleBarView；
        // 两者都走 ITitleBarHost 窄接口，不接触 QMainWindow。
        ITitleBarView* view = nullptr;
        if (m_shellServices.titleBarFactory)
        {
            QWidget* widget = m_shellServices.titleBarFactory->createTitleBar(spec, m_titleBarHostAdapter.get(), host);
            // 客户标题栏可以是纯 QWidget（只做外观），也可以同时实现 ITitleBarView
            // （额外接收宿主与最大化状态）；后者用接口指针保留下来。
            view = dynamic_cast<ITitleBarView*>(widget);
            m_titleBarWidget = widget;
        }
        else
        {
            view = createTitleBar(spec, m_titleBarHostAdapter.get(), host);
            m_titleBarWidget = view ? view->widget() : nullptr;
        }

        if (!m_titleBarWidget)
            return;

        // 保留 createTitleBar 返回的视图接口，不再从控件反推：
        // 视图的 widget() 允许与视图本身是不同对象，反推会丢掉接口能力。
        // 控件所有权归父控件（host），视图生命周期不长于其 widget()，
        // 因此这里不调用 destroy()。
        m_titleBarView = view;
        m_titleBarMenuHost = dynamic_cast<ITitleBarMenuHost*>(m_titleBarWidget.data());
        host->setCenterWidget(m_titleBarWidget.data());
    }

    void ShellView::bindTitleBarToWindow()
    {
        if (!m_window)
            return;

        // attach / rebuild / refreshShell 唯一的绑定入口：
        // 适配器在这里解绑旧窗口、重装状态监听，并把最大化状态推给新视图。
        m_titleBarHostAdapter->bind(m_window, m_frame.get(), m_titleBarView);
    }

    void ShellView::refreshShell()
    {
        if (!m_shell)
            return;

        // 面板控件的文字走 IRetranslatable，不需要重建；只有标题栏/菜单在创建时
        // 就把文字写进控件，必须按新语种的计划重新生成。
        for (RegionContainer* host : m_regContainers)
        {
            if (host->type() == RegionType::TitleBar)
            {
                // 先解绑再销毁控件：适配器不能保留指向已删除视图的指针。
                m_titleBarHostAdapter->bind(nullptr, nullptr, nullptr);
                m_titleBarWidget = nullptr;
                m_titleBarView = nullptr;
                m_titleBarMenuHost = nullptr;
                host->clearCenterWidget();
                createTitleBarInto(host);
            }
            else if (host->type() == RegionType::MenuBar)
            {
                host->clearCenterWidget();
                if (QMenuBar* menuBar = createMenuBar(host))
                    host->setCenterWidget(menuBar);
            }
        }

        // 新标题栏替换了旧控件：先按当前模式合并菜单，再统一重绑适配器。
        syncMenuPlacement();
        bindTitleBarToWindow();

        // 命令动作没有重建（工具栏与面板按钮还在用它们）：按新语言 / 新开关原地刷新文字与状态。
        m_cmdActions.refresh();
    }

    bool ShellView::embedsMenuInTitleBar() const
    {
        // 菜单内嵌是可选的标题栏特性：实现 ITitleBarMenuHost 才参与，
        // 与「框架自带 / 客户工厂」无关，装配层不需要记两种分支
        if (!m_titleBarMenuHost || m_assemblyInfo.bNativeTitleBar || m_assemblyInfo.menus.isEmpty())
            return false;

        // 只有真正生成了菜单栏区域才谈得上「内嵌」，否则保持不变。
        for (RegionContainer* host : m_regContainers)
        {
            if (host->type() == RegionType::MenuBar)
                return true;
        }
        return false;
    }

    void ShellView::syncMenuPlacement()
    {
        RegionContainer* menuHost = nullptr;
        for (RegionContainer* host : m_regContainers)
        {
            if (host->type() == RegionType::MenuBar)
            {
                menuHost = host;
                break;
            }
        }
        if (!menuHost)
            return;

        if (!embedsMenuInTitleBar())
        {
            if (!menuHost->isVisible())
                menuHost->show();

            return;
        }

        QWidget* menuBar = menuHost->takeCenterWidget();
        if (!menuBar)
            return;

        m_titleBarMenuHost->setMenuWidget(menuBar);
        menuHost->hide();
    }

    QMenuBar* ShellView::createMenuBar(QWidget* parent)
    {
        // 菜单构建与信号绑定全部交给 MenuController：
        // 运行期只关心「有没有菜单栏」以及它挂在哪里。
        return m_menuCtrl.build(m_assemblyInfo, parent);
    }

    void ShellView::showMessage(const QString& text)
    {
        if (!m_shell)
            return;

        if (auto* view = dynamic_cast<IStatusMessageView*>(m_shell.data()))
            view->showStatusMessage(text);

        const QList<QWidget*> widgets = m_shell->findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            if (auto* view = dynamic_cast<IStatusMessageView*>(widget))
                view->showStatusMessage(text);
        }
    }

    HostContext ShellView::hostContext() const
    {
        HostContext context;
        context.strThemeId = m_shellServices.strThemeId;
        if (m_shellServices.transService)
            context.strLanguageId = m_shellServices.transService->language();

        if (m_shellServices.featureFlags)
            context.nFeatureRevision = m_shellServices.featureFlags->revision();

        context.text = m_shellServices.text;
        return context;
    }

    void ShellView::notifyHostContext()
    {
        if (!m_shell)
            return;

        const HostContext context = hostContext();
        // shell 自身也可能需要上下文；随后的遍历覆盖全部子孙控件。
        if (auto* aware = dynamic_cast<IHostContextAware*>(m_shell.data()))
            aware->setHostContext(context);

        const QList<QWidget*> widgets = m_shell->findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            if (auto* aware = dynamic_cast<IHostContextAware*>(widget))
                aware->setHostContext(context);
        }
    }

    void ShellView::retranslate()
    {
        // 先让控件拿到新语言/皮肤，再触发文案重算：两者都基于最新上下文。
        notifyHostContext();
        if (!m_shell)
            return;

        const QList<QWidget*> widgets = m_shell->findChildren<QWidget*>();
        for (QWidget* widget : widgets)
        {
            if (auto* retranslatable = dynamic_cast<IRetranslatable*>(widget))
                retranslatable->retranslate();
        }
    }
}  // namespace RD