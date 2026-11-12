/**
 * @file MenuDispatcher.cpp
 * @brief 窗口级命令分发器实现
 */
#include "MenuDispatcher.h"

#include "WorkbenchMenuManager.h"
#include "WorkbenchWindow.h"
#include "UiWorkbench.h"
#include "UiStateCenter.h"
#include "BuildConfig.h"
#include "Log/AprLogger.h"
#include "UI/Safety/EmergencyStopFeature.h"
#include "UI/Dlg/AboutDialog.h"
#include "UI/Services/HelpDialogService.h"
#include "UI/ThemeManager.h"
#include "UI/LanguageManager.h"
#include "Composition/ApplicationCompositionRoot.h"
#include "UI/Safety/EmergencyStopController.h"
#include "UI/Settings/SettingsService.h"

#include <QCoreApplication>
#include <QMessageBox>
#include <QStatusBar>

MenuDispatcher::MenuDispatcher(QObject* parent)
    : QObject(parent)
{
}

void MenuDispatcher::setSelf(WorkbenchMenuManager* self)
{
    m_self = self;
}

void MenuDispatcher::setWorkbench(UiWorkbench* workbench)
{
    m_workbench = workbench;
}

void MenuDispatcher::setWorkbenchWindow(WorkbenchWindow* window)
{
    Q_UNUSED(window);
}

void MenuDispatcher::setStateCenter(UiStateCenter* stateCenter)
{
    Q_UNUSED(stateCenter);
}

bool MenuDispatcher::isWorkbenchSwitchCommand(const QString& commandId)
{
    return commandId == QLatin1String("view.switch_to_2d") || commandId == QLatin1String("view.switch_to_3d");
}

bool MenuDispatcher::isThemeCommand(const QString& commandId)
{
    return commandId.startsWith(QLatin1String("theme."));
}

bool MenuDispatcher::isLanguageCommand(const QString& commandId)
{
    return commandId.startsWith(QLatin1String("language."));
}

bool MenuDispatcher::isWindowLevelCommand(const QString& commandId)
{
    // 唯一真相：MenuDispatcher::dispatch 短路处理的完整命令集合
    // （工作台切换 / 主题 / 语言 / 关于）。改这里即同时改变三处行为：
    // 「按钮是否可点」（isCommandRegistered）、「过滤是否放行」
    // （rebuildMenusFromConfig 的 commandAvailable）与「契约测试/自检是否跳过
    // 目录校验」（CommandUiWiringTests、UiConfigSelfCheck）。不要再开第二份名单。
    return isWorkbenchSwitchCommand(commandId) || isThemeCommand(commandId) || isLanguageCommand(commandId) ||
        commandId == QLatin1String("help.about");
}

bool MenuDispatcher::isCommandRegistered(const QString& commandId) const
{
    if (isWindowLevelCommand(commandId))
    {
        // 3D 未编译时，禁用切换到 3D 的命令
        if (commandId == QLatin1String("view.switch_to_3d") && !BuildConfig::kUi3D)
        {
            return false;
        }
        return true;
    }
    return m_workbench && m_workbench->isCommandRegistered(commandId);
}

void MenuDispatcher::dispatch(const QString& commandId, const QVariantMap& params)
{
    // 工作台切换统一由主窗口 triggerWorkbench 处理（含防重复切换保护）。
    if (isWorkbenchSwitchCommand(commandId) && m_self && m_self->workbenchWindow())
    {
        const QString target =
            commandId == QLatin1String("view.switch_to_3d") ? QStringLiteral("3D") : QStringLiteral("2D");
        m_self->workbenchWindow()->triggerWorkbench(target);
        return;
    }

    // 主题切换
    if (isThemeCommand(commandId) && m_self && m_self->workbenchWindow())
    {
        const QString themeId = commandId.mid(QStringLiteral("theme.").size());
        m_self->workbenchWindow()->triggerTheme(themeId);
        return;
    }

    // 语言切换
    if (isLanguageCommand(commandId) && m_self && m_self->workbenchWindow())
    {
        QString code = commandId.mid(QStringLiteral("language.").size());
        if (code == QLatin1String("system"))
        {
            code.clear();
        }
        AppLanguage lang = AppLanguage::English;
        if (const auto parsed = LanguageManager::fromCode(code); parsed.has_value())
        {
            lang = *parsed;
        }
        else
        {
            APR_WARNF("[MenuDispatcher] unknown language code '%s', ignore", qPrintable(code));
            return;
        }
        if (auto* settings = ApplicationCompositionRoot::getSettingsService(); settings && settings->isInitialized())
        {
            if (!settings->setLanguage(lang))
            {
                QMessageBox::warning(
                    m_self ? m_self->workbenchWindow() : nullptr,
                    QObject::tr("Language"),
                    QObject::tr("Failed to load language resources; language unchanged"));
            }
        }
        else
        {
            LanguageManager::instance()->setLanguage(lang);
        }
        return;
    }

    // 关于对话框：help.about 是窗口级动作，在进命令总线前短路
    // （目录里保留条目仅供兼容，不作为分发路径）。
    // 模式必须取当前工作台 —— 硬编码 Mode2D 会让 3D 下打开 2D 版 About。
    if (commandId == QLatin1String("help.about") && m_self && m_self->workbenchWindow())
    {
        const UiWorkbench* wb = m_self->workbenchWindow()->currentWorkbench();
        const AppMode mode = (wb && wb->id() == QLatin1String("3D")) ? AppMode::Mode3D : AppMode::Mode2D;
        AboutDialog::showDialog(mode, m_self->workbenchWindow());
        return;
    }

    // 急停只读态：除安全、视图、帮助与保存外，其余命令一律拦下并提示，
    // 避免操作员在未完成复位流程时继续编辑或启动新的加工。
    // 总开关关闭时整段失效（软件尚未交付急停硬件链路，v1.1 开启）；
    // 拦截日志保留在分支内，重新开启后可直接用于定位
    auto* root = ApplicationCompositionRoot::instance();
    auto* safety = root ? root->emergencyStopController() : nullptr;
    if (EmergencyStopFeature::kEnabled && safety && safety->isReadOnly())
    {
        const bool allowed = commandId == QLatin1String("laser.emergency_stop") ||
            commandId == QLatin1String("laser.reset_safety") || commandId.startsWith(QLatin1String("view.")) ||
            commandId.startsWith(QLatin1String("help.")) || commandId == QLatin1String("file.save") ||
            commandId == QLatin1String("file.save_as");
        if (!allowed)
        {
            // 拦截必须留痕：之前只在状态栏闪 5 秒，日志里没有任何记录，
            // 现场表现为"菜单点了完全没反应"，无法定位
            APR_WARNF("[MenuDispatcher] Command blocked by emergency stop read-only: '%s'",
                commandId.toUtf8().constData());
            if (m_self && m_self->workbenchWindow())
            {
                // 文案要指向复位入口，时长拉长到 15 秒，避免被误判成软件故障
                m_self->workbenchWindow()->statusBar()->showMessage(
                    QCoreApplication::translate(
                        "MenuDispatcher", "Emergency stop active. Complete reset before editing or processing."),
                    15000);
            }
            return;
        }
    }

    // 非窗口级命令：转发给当前工作台（isCommandRegistered 已按工作台目录裁决过
    // 按钮可用性，这里落到 2D/3D 各自的 OperationBus）。
    // 注意：UiLayoutBuilder 把菜单/快捷键直接绑到本 dispatcher，这条转发是唯一
    // 通路 —— P2-1 拆分时曾把它丢失，表现为 3D 下 file.import_obj 等命令
    // 只留一条 unhandled 日志而毫无动作。
    if (!m_workbench)
    {
        APR_WARNF("[MenuDispatcher] No active workbench for command='%s'", commandId.toUtf8().constData());
        return;
    }
    m_workbench->dispatchCommand(commandId, params);
}