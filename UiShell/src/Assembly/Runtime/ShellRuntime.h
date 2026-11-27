#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <memory>

#include "Assembly/Runtime/ProfilePipeline.h"
#include "UiShell/Assembly/IShellRuntime.h"
#include "UiShell/Assembly/ShellServices.h"
#include "Model/Assembly/AssemblyInfo.h"

class QMainWindow;
class QWidget;

namespace RD
{
    class CommandActions;
    class DialogController;
    class MenuController;
    class ShellView;

    // 运行时
    class ShellRuntime : public IShellRuntime
    {
    public:
        explicit ShellRuntime(const ShellServices& services);
        ~ShellRuntime() override;

        ShellRuntime(const ShellRuntime&) = delete;
        ShellRuntime& operator=(const ShellRuntime&) = delete;

    public:
        // 从 UTF-8 JSON 文件加载并重建界面
        bool loadFromFile(const QString& filePath) override;
        // 加载 UTF-8 JSON 文本并重建界面
        bool loadFromJson(const QByteArray& json) override;

        QWidget* shell() const override;

        // 将 shell 的所有权交给调用方
        QWidget* takeShell() override;

        // 接管顶层窗口
        bool attachWindow(QMainWindow* window) override;

        // 语言切换
        void switchLanguage(const QString& language) override;
        // 重译所有 IRetranslatable 控件
        void retranslate() override;

        // 切换皮肤
        void switchTheme(const QString& themeId) override;

        QString theme() const override
        {
            return m_shellServices.strThemeId;
        }

        // 装配计划仅供 shell 内部使用，不在 IShellRuntime 上暴露。
        const AssemblyInfo& info() const
        {
            return m_profilePipeline.info();
        }

        const QList<Diagnostic>& diagnostics() const override
        {
            return m_profilePipeline.diagnostics();
        }

        QString lastError() const override
        {
            return m_profilePipeline.lastError();
        }

        QString lastMessage() const override
        {
            return m_strLastMsg;
        }

        // 按区域 strId 查找区域容器
        IRegionContainer* regionContainer(const QString& regionId) const override;

        // 查找承载指定面板的区域容器
        IRegionContainer* regionContainerForPanel(const QString& panelId) const override;

        // IShellHost
        void invoke(const QString& commandId) override;
        void invokeWithArgs(const QString& commandId, const QJsonObject& args) override;
        QAction* commandAction(const QString& commandId) override;
        void refreshCommands() override;
        bool activatePanel(const QString& panelId) override;
        void showMessage(const QString& text) override;
        DialogResult openDialog(const QString& dialogId, QWidget* parent, const QJsonObject& params = {}) override;

        void destroy() override
        {
            delete this;
        }

    private:
        // 开关变化是全量重新规划：requiredFeatures 影响面板可见性，
        // 只刷新菜单置灰会留下与快照不一致的界面。重建 shell 会丢弃面板实例状态。
        void onFeatureFlagsChanged();

        // 命令分发（客户代码绑定表 → ICommandInvoker）；invoke() 在它之后刷新命令状态。
        void dispatch(const QString& commandId);

        // 交给面板（PanelCreateRequest::host）的窄宿主：只转发 IShellHost 方法，
        // 避免面板直接持有 ShellRuntime 引用。
        class PanelHost final : public IShellHost
        {
        public:
            explicit PanelHost(ShellRuntime& runtime)
                : m_runtime(runtime)
            {
            }

            void invoke(const QString& commandId) override
            {
                m_runtime.invoke(commandId);
            }

            void invokeWithArgs(const QString& commandId, const QJsonObject& args) override
            {
                m_runtime.invokeWithArgs(commandId, args);
            }

            QAction* commandAction(const QString& commandId) override
            {
                return m_runtime.commandAction(commandId);
            }

            void refreshCommands() override
            {
                m_runtime.refreshCommands();
            }

            bool activatePanel(const QString& panelId) override
            {
                return m_runtime.activatePanel(panelId);
            }

            void showMessage(const QString& text) override
            {
                m_runtime.showMessage(text);
            }

            DialogResult openDialog(const QString& dialogId, QWidget* parent, const QJsonObject& params = {}) override
            {
                return m_runtime.openDialog(dialogId, parent, params);
            }

        private:
            ShellRuntime& m_runtime;
        };

        // 功能开关观察者：转发到 onFeatureFlagsChanged()。
        class FeatureObserver final : public IFeatureFlagObserver
        {
        public:
            explicit FeatureObserver(ShellRuntime& runtime)
                : m_runtime(runtime)
            {
            }

            void onFeatureFlagsChanged() override
            {
                m_runtime.onFeatureFlagsChanged();
            }

        private:
            ShellRuntime& m_runtime;
        };

    private:
        ShellServices m_shellServices;
        PanelHost m_panelHost{ *this };                 // 面板宿主
        ProfilePipeline m_profilePipeline;
        QString m_strLastMsg;

        std::unique_ptr<ShellView> m_shellView;
        std::unique_ptr<MenuController> m_menuCtrl;     // 菜单
        std::unique_ptr<DialogController> m_dlgCtrl;    // 对话框
        std::unique_ptr<CommandActions> m_cmdActions;   // 命令动作

        // 开关订阅：store 变化时重新规划；析构时退订。
        FeatureObserver m_ftObserver{ *this };
        quint64 m_ftSubscriptionId = 0;
    };
}  // namespace RD
