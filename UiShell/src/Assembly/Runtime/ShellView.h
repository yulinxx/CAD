#pragma once

#include <QList>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

#include "Assembly/Commands/CommandToolBar.h"
#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/HostContext.h"
#include "Model/Assembly/AssemblyInfo.h"
#include "Model/Profile/ProfileLoadResult.h"

class QMainWindow;
class QMenuBar;
class QWidget;

namespace AA
{
    class CommandActions;
    class IShellHost;
    class IWindowFrame;
    class IRegionContainer;
    class ITitleBarMenuHost;
    class ITitleBarView;
    class MenuController;
    class RegionContainer;
    class TitleBarHostAdapter;

    // shell 视图
    class ShellView
    {
    public:
        ShellView(const ShellServices& services,
            const AssemblyInfo& info,
            MenuController& menuController,
            CommandActions& commandActions,
            IShellHost* host,
            std::function<void(const QString&)> reportMessage);

        ~ShellView();

        ShellView(const ShellView&) = delete;
        ShellView& operator=(const ShellView&) = delete;

    public:
        QWidget* shell() const;

        // 将 shell 的所有权交给调用方；转移后 ShellView 不再删除它。
        QWidget* takeShell();

        // 主窗口
        bool attachWindow(QMainWindow* window);
        QMainWindow* window() const
        {
            return m_window;
        }

        // 销毁旧 shell 并按当前计划重建
        void rebuild();

        // 只重建标题栏与菜单栏
        void refreshShell();

        // 只重建中央视图：客户切换工作台形态后复用同一装配计划。
        void refreshCenterView();

        // 装配诊断
        const QList<Diagnostic>& assemblyDiagnostics() const
        {
            return m_assemblyDiagnostics;
        }

        IRegionContainer* regionContainer(const QString& regionId) const;
        IRegionContainer* regionContainerForPanel(const QString& panelId) const;

        // 把当前宿主上下文推给 shell 内所有 IHostContextAware 控件。
        void notifyHostContext();
        // 先推上下文，再调用所有 IRetranslatable::retranslate。
        void retranslate();

        // 把提示消息推给 shell 内所有 IStatusMessageView 控件（通常是状态栏面板）。
        void showMessage(const QString& text);

    private:
        RegionContainer* createRegionContainer(const RegionInfo& region, QWidget* parent);
        // 面板工厂：注册表优先（客户可以替换 shell 自带的），找不到时用 shell 自带的通用面板（msui.toolbar）。
        IWidgetFactory* findFactory(const QString& factoryId);
        void createTitleBarInto(RegionContainer* host);
        QMenuBar* createMenuBar(QWidget* parent);
        // attach / rebuild / refresh 共用的唯一定点：把当前窗口、框架与标题栏绑在一起。
        void bindTitleBarToWindow();
        bool embedsMenuInTitleBar() const;
        void syncMenuPlacement();
        HostContext hostContext() const;

    private:

        const ShellServices& m_shellServices;
        const AssemblyInfo& m_assemblyInfo;
        MenuController& m_menuCtrl;
        CommandActions& m_cmdActions;
        IShellHost* m_host = nullptr;

        // shell 的通用工具栏面板
        CommandToolBarFactory m_toolBarFactory;

        // 运行期状态消息经回调交给 ShellRuntime，供宿主通过 lastMessage() 查询
        std::function<void(const QString&)> m_reportMsgFn;

        // 装配期错误列表
        QList<Diagnostic> m_assemblyDiagnostics;

        // shell 可能先于 ShellView 被顶层窗口销毁，用 QPointer 避免悬空
        QPointer<QWidget> m_shell;

        // 窗口中央容器由顶层窗口持有；重建 shell 时需要把新 shell 放回去。
        QPointer<QWidget> m_container;
        QList<RegionContainer*> m_regContainers;

        // 标题栏控件、视图契约与可选菜单宿主特性分开保存
        QPointer<QWidget> m_titleBarWidget;
        ITitleBarView* m_titleBarView = nullptr;
        ITitleBarMenuHost* m_titleBarMenuHost = nullptr;
        QMainWindow* m_window = nullptr;

        std::unique_ptr<IWindowFrame> m_frame;
        std::unique_ptr<TitleBarHostAdapter> m_titleBarHostAdapter;
    };
}  // namespace AA
