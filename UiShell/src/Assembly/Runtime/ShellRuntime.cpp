#include "Assembly/Runtime/ShellRuntime.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QMenu>
#include <QStringList>
#include <QWidget>

#include <optional>
#include <QMainWindow>

#include "Assembly/Commands/CommandActions.h"
#include "Assembly/Host/ThemeController.h"
#include "Assembly/Menus/MenuController.h"
#include "Assembly/Runtime/ShellView.h"
#include "Assembly/Dialogs/DialogController.h"
#include "UiShell/Assembly/IRegionContainer.h"
#include "UiShell/i18n/ITranslationService.h"
#include "UiShell/i18n/LanguageArgument.h"

// 显式引用生成函数，保证样式表资源被使用
static void rdInitShellResources()
{
    Q_INIT_RESOURCE(UiShell);
}

namespace
{
    // 外置 profile 里的相对路径按 profile 所在目录解析；:/ 与绝对路径原样保留。
    QString resolveProfileAsset(const QString& path, const QString& profileDir)
    {
        if (path.isEmpty() || path.startsWith(QLatin1Char(':')) || QFileInfo(path).isAbsolute())
            return path;
        return QDir(profileDir).absoluteFilePath(path);
    }

    QString profileTranslationDir(const QString& profileDir)
    {
        if (profileDir.isEmpty())
            return QString();
        const QDir dir(profileDir);
        return dir.exists(QStringLiteral("translations"))
            ? dir.absoluteFilePath(QStringLiteral("translations")) : QString();
    }

    QString profileStylesDir(const QString& profileDir)
    {
        if (profileDir.isEmpty())
            return QString();
        const QDir dir(profileDir);
        return dir.exists(QStringLiteral("styles"))
            ? dir.absoluteFilePath(QStringLiteral("styles")) : QString();
    }
}  // namespace

namespace AA
{
    ShellRuntime::ShellRuntime(const ShellServices& services)
        : m_shellServices(services)
    {
        rdInitShellResources();

        m_dlgCtrl = std::make_unique<DialogController>(&m_shellServices, &m_profilePipeline.profileDir());

        // 所有命令入口
        m_cmdActions = std::make_unique<CommandActions>(&m_shellServices, [this](const QString& cmdId) {
            invoke(cmdId);
            });

        m_menuCtrl = std::make_unique<MenuController>(
            &m_shellServices,
            *m_cmdActions,
            [this](const QString& language) {
                switchLanguage(language);
            },
            [this](const QString& themeId) {
                switchTheme(themeId);
            });

        m_shellServices.strThemeId = m_shellServices.strThemeId.trimmed().toLower();
        if (m_shellServices.strThemeId.isEmpty())
            m_shellServices.strThemeId = QString::fromLatin1(ThemeId::Light);

        m_shellView = std::make_unique<ShellView>(m_shellServices,
            m_profilePipeline.info(),
            *m_menuCtrl,
            *m_cmdActions,
            &m_panelHost,
            [this](const QString& message) {
                m_strLastMsg = message;
            });

        // 默认皮肤
        if (!ThemeController::apply(m_shellServices.strThemeId,
                m_shellServices.strCustomStyle, m_shellServices.strStyleDir))
        {
            m_strLastMsg = QStringLiteral("style sheet '%1' not found").arg(m_shellServices.strThemeId);

            const QString light = QString::fromLatin1(ThemeId::Light);
            if (m_shellServices.strThemeId != light
                && ThemeController::apply(light, m_shellServices.strCustomStyle, m_shellServices.strStyleDir))
                m_shellServices.strThemeId = light;
        }

        // 订阅开关变化
        if (m_shellServices.featureFlags)
        {
            m_ftSubscriptionId = m_shellServices.featureFlags->subscribe(&m_ftObserver);
        }
    }

    ShellRuntime::~ShellRuntime()
    {
        // 先退订开关变化,避免悬空
        if (m_shellServices.featureFlags && m_ftSubscriptionId != 0)
        {
            m_shellServices.featureFlags->unsubscribe(m_ftSubscriptionId);
            m_ftSubscriptionId = 0;
        }
    }

    QWidget* ShellRuntime::shell() const
    {
        return m_shellView->shell();
    }

    QWidget* ShellRuntime::takeShell()
    {
        return m_shellView->takeShell();
    }

    bool ShellRuntime::attachWindow(QMainWindow* window)
    {
        const bool attached = m_shellView->attachWindow(window);
        if (attached)
        {
            m_window = window;
            applyBrandToWindow();
        }
        return attached;
    }

    bool ShellRuntime::loadFromJson(const QByteArray& json)
    {
        m_translationDir.clear();
        m_shellServices.brandParams = QJsonObject{};
        // strStyleDir 可能由宿主在构造时给初值，纯 JSON 加载不覆盖它。
        const bool bValid = m_profilePipeline.loadFromJson(json, m_shellServices);

        // 加载失败重建空 shell
        m_shellView->rebuild();
        m_profilePipeline.setAssemblyDiagnostics(m_shellView->assemblyDiagnostics());
        if (bValid)
        {
            applyBrand();
            applyBrandToWindow();
        }
        return bValid;
    }

    bool ShellRuntime::loadFromFile(const QString& filePath)
    {
        m_translationDir.clear();
        m_shellServices.brandParams = QJsonObject{};
        const bool bValid = m_profilePipeline.loadFromFile(filePath, m_shellServices);
        m_shellView->rebuild();
        m_profilePipeline.setAssemblyDiagnostics(m_shellView->assemblyDiagnostics());
        if (bValid)
        {
            m_translationDir = profileTranslationDir(m_profilePipeline.profileDir());
            m_shellServices.strStyleDir = profileStylesDir(m_profilePipeline.profileDir());
            applyBrand();
            applyBrandToWindow();
        }
        return bValid;
    }

    bool ShellRuntime::hasExplicitLanguageArgument()
    {
        return AA::hasExplicitLanguageArgument();
    }

    void ShellRuntime::applyBrand()
    {
        m_shellServices.brandParams = QJsonObject{};
        const BrandSpec* brand = m_profilePipeline.brand();
        if (!brand)
            return;

        // 关于框等共享对话框读取这些参数；logo 的相对路径在这里解析一次。
        const auto putParam = [this, brand](const char* key, const QString& value) {
            if (!value.isEmpty())
                m_shellServices.brandParams.insert(QString::fromLatin1(key), value);
            };
        putParam(DialogParam::Product, brand->strProduct);
        putParam(DialogParam::Company, brand->strOrganization);
        putParam(DialogParam::Copyright, brand->strCopyright);
        putParam(DialogParam::Website, brand->strWebsite);
        putParam(DialogParam::Email, brand->strEmail);
        putParam(DialogParam::License, brand->strLicense);
        putParam(DialogParam::LogoPath,
            resolveProfileAsset(brand->strLogo, m_profilePipeline.profileDir()));

        if (!brand->strOrganization.isEmpty())
            QCoreApplication::setOrganizationName(brand->strOrganization);

        // 主题在 ShellRuntime 构造时已按客户默认值应用；profile 可再覆盖。
        // force=true：即使 id 相同也要重新加载，外置 styles/ 可能带来新皮肤文件。
        if (!brand->strTheme.isEmpty())
            applyTheme(brand->strTheme, true);

        // 命令行显式语言优先；未指定时才用 profile 的默认语言。
        if (!brand->strLanguage.isEmpty() && !hasExplicitLanguageArgument()
            && m_shellServices.transService
            && m_shellServices.transService->language()
                != m_shellServices.transService->normalizeLanguageId(brand->strLanguage))
        {
            switchLanguage(brand->strLanguage);
        }
    }

    void ShellRuntime::applyBrandToWindow()
    {
        if (!m_window || !m_profilePipeline.hasModel())
            return;

        const BrandSpec* brand = m_profilePipeline.brand();
        const AssemblyInfo& info = m_profilePipeline.info();

        if (brand && !brand->strProduct.isEmpty())
            m_window->setWindowTitle(brand->strProduct);
        else if (!info.titleBar.strTitle.isEmpty())
            m_window->setWindowTitle(info.titleBar.strTitle);

        const QString iconPath = (brand && !brand->strIcon.isEmpty())
            ? brand->strIcon : info.titleBar.strIcon;
        const QString resolved = resolveProfileAsset(iconPath, m_profilePipeline.profileDir());
        if (!resolved.isEmpty())
        {
            const QIcon icon(resolved);
            if (!icon.isNull())
                m_window->setWindowIcon(icon);
        }
    }

    IRegionContainer* ShellRuntime::regionContainer(const QString& regionId) const
    {
        return m_shellView->regionContainer(regionId);
    }

    IRegionContainer* ShellRuntime::regionContainerForPanel(const QString& panelId) const
    {
        return m_shellView->regionContainerForPanel(panelId);
    }

    void ShellRuntime::onFeatureFlagsChanged()
    {
        // 上一次加载失败时没有模型可规划；空界面不需要响应开关变化
        if (!m_profilePipeline.hasModel())
            return;

        // 忽略重复通知
        if (m_shellServices.featureFlags &&
            m_shellServices.featureFlags->revision() == m_profilePipeline.featureRevision())
            return;

        const QString before = m_profilePipeline.info().structureKey();
        m_profilePipeline.replan(m_shellServices);
        const QString after = m_profilePipeline.info().structureKey();

        if (before == after)
        {
            m_shellView->refreshShell();
            m_shellView->notifyHostContext();
            return;
        }

        m_shellView->rebuild();
        m_profilePipeline.setAssemblyDiagnostics(m_shellView->assemblyDiagnostics());
    }

    void ShellRuntime::invoke(const QString& cmdId)
    {
        dispatch(cmdId);
        m_cmdActions->refresh();
    }

    void ShellRuntime::invokeWithArgs(const QString& cmdId, const QJsonObject& args)
    {
        if (m_shellServices.cmdInvoker)
            m_shellServices.cmdInvoker->invokeWithArgs(cmdId, args);
        else
            m_strLastMsg = QStringLiteral("no ICommandInvoker configured for '%1'").arg(cmdId);

        m_cmdActions->refresh();
    }

    QAction* ShellRuntime::commandAction(const QString& commandId)
    {
        return m_cmdActions->action(commandId);
    }

    void ShellRuntime::refreshCommands()
    {
        m_cmdActions->refresh();
    }

    QMenu* ShellRuntime::createContextMenu(const QString& contextMenuId, QWidget* parent)
    {
        const auto it = m_profilePipeline.info().contextMenus.constFind(contextMenuId);
        if (it == m_profilePipeline.info().contextMenus.constEnd() || it.value().isEmpty())
        {
            m_strLastMsg = QStringLiteral("unknown context menu '%1'").arg(contextMenuId);
            return nullptr;
        }

        // 弹出前统一刷新一次：右键项与菜单 / 工具栏 / 面板看到的是同一份命令状态。
        m_cmdActions->refresh();
        return m_menuCtrl->buildContextMenu(it.value(), parent);
    }

    void ShellRuntime::showContextMenu(const QString& contextMenuId, const QPoint& globalPos, QWidget* anchor)
    {
        Q_UNUSED(anchor);
        std::unique_ptr<QMenu> menu(createContextMenu(contextMenuId, nullptr));
        if (menu)
            menu->exec(globalPos);
    }

    void ShellRuntime::dispatch(const QString& cmdId)
    {
        std::optional<DialogCmdBinding> binding;
        for (const DialogCmdBinding& candidate : m_shellServices.dialogBindings)
        {
            if (candidate.strCmd == cmdId)
            {
                binding = candidate;
                break;
            }
        }

        if (binding)
        {
            const DialogResult result = m_dlgCtrl->openResult(
                binding->strDlgId, m_shellView->window(), &m_strLastMsg, binding->params);

            if (binding->continuePolicy == DialogContinuePolicy::Never)
                return;
            if (result.status != DialogResult::Accepted)
                return;

            if (m_shellServices.cmdInvoker)
            {
                m_shellServices.cmdInvoker->invokeWithArgs(cmdId, result.data);
                return;
            }
        }
        else if (m_shellServices.cmdInvoker)
        {
            // 未绑定
            m_shellServices.cmdInvoker->invoke(cmdId);
            return;
        }

        // 未注入调用器时只记录提示
        m_strLastMsg = QStringLiteral("no ICommandInvoker configured for '%1'").arg(cmdId);
    }

    bool ShellRuntime::activatePanel(const QString& panelId)
    {
        IRegionContainer* host = regionContainerForPanel(panelId);
        if (!host)
            return false;

        QWidget* widget = host->panelWidget(panelId);
        if (!widget)
            return false;

        // 面板可能被其他面板遮挡
        widget->show();
        widget->raise();
        return true;
    }

    void ShellRuntime::showMessage(const QString& text)
    {
        m_strLastMsg = text;
        m_shellView->showMessage(text);
    }

    DialogResult ShellRuntime::openDialog(const QString& dialogId, QWidget* parent, const QJsonObject& params)
    {
        // 与命令绑定走同一条查找链路；结果原样返回，不再序列化成字节流。
        return m_dlgCtrl->openResult(dialogId, parent ? parent : m_shellView->window(), &m_strLastMsg, params);
    }

    void ShellRuntime::switchLanguage(const QString& language)
    {
        if (!m_shellServices.transService)
            return;

        // 外置 profile 可自带 translations/；命令行语言仍由 TranslationSetup 提前装载。
        if (!m_shellServices.transService->install(language, m_translationDir))
            m_strLastMsg = m_shellServices.transService->lastError();

        if (m_profilePipeline.hasModel())
        {
            m_profilePipeline.replan(m_shellServices);
            m_shellView->refreshShell();
        }
        else if (!m_profilePipeline.json().isEmpty())
        {
            const QByteArray json = m_profilePipeline.json();
            loadFromJson(json);
        }
        else
        {
            m_shellView->rebuild();
            m_profilePipeline.setAssemblyDiagnostics(m_shellView->assemblyDiagnostics());
        }

        m_shellView->retranslate();

        if (m_shellServices.shellListener)
            m_shellServices.shellListener->onLanguageChanged(m_shellServices.transService->language());

        // 宿主回调可能把窗口标题改回客户模块默认名；profile 品牌优先。
        applyBrandToWindow();
    }

    void ShellRuntime::switchTheme(const QString& themeId)
    {
        applyTheme(themeId, false);
    }

    void ShellRuntime::applyTheme(const QString& themeId, bool force)
    {
        const QString normalized = themeId.trimmed().toLower();
        if (normalized.isEmpty() || (!force && normalized == m_shellServices.strThemeId))
            return;

        if (!ThemeController::apply(normalized,
                m_shellServices.strCustomStyle, m_shellServices.strStyleDir))
            return;

        m_shellServices.strThemeId = normalized;
        m_shellView->refreshShell();
        m_shellView->notifyHostContext();
    }

    void ShellRuntime::retranslate()
    {
        m_shellView->retranslate();
    }

    void ShellRuntime::refreshCenterView()
    {
        if (m_shellView)
            m_shellView->refreshCenterView();
    }

    IShellRuntime* createShellRuntime(const ShellServices& services)
    {
        return new ShellRuntime(services);
    }
}  // namespace AA