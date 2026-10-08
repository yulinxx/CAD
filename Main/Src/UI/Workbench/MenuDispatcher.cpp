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
#include "Log/SyLogger.h"
#include "UI/Dlg/AboutDialog.h"
#include "UI/Services/HelpDialogService.h"
#include "UI/ThemeManager.h"
#include "UI/LanguageManager.h"
#include "Composition/ApplicationCompositionRoot.h"
#include "UI/Settings/SettingsService.h"

#include <QCoreApplication>

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
    m_workbenchWindow = window;
}

void MenuDispatcher::setStateCenter(UiStateCenter* stateCenter)
{
    m_stateCenter = stateCenter;
}

bool MenuDispatcher::isWorkbenchSwitchCommand(const QString& commandId)
{
    return commandId == QLatin1String("view.switch_to_2d") ||
        commandId == QLatin1String("view.switch_to_3d");
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
    return isWorkbenchSwitchCommand(commandId) || isThemeCommand(commandId) ||
        isLanguageCommand(commandId) || commandId == QLatin1String("help.about");
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
    Q_UNUSED(params);

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
            SY_WARNF("[MenuDispatcher] unknown language code '%s', ignore", qPrintable(code));
            return;
        }
        if (auto* settings = ApplicationCompositionRoot::getSettingsService(); settings && settings->isInitialized())
        {
            settings->setLanguage(lang);
        }
        else
        {
            LanguageManager::instance()->setLanguage(lang);
        }
        return;
    }

    // 关于对话框：help.about 是窗口级动作，在进命令总线前短路
    //（目录里保留条目仅供历史兼容，不作为分发路径）。
    // 模式必须取当前工作台 —— 硬编码 Mode2D 会让 3D 下打开 2D 版 About。
    if (commandId == QLatin1String("help.about") && m_self && m_self->workbenchWindow())
    {
        const UiWorkbench* wb = m_self->workbenchWindow()->currentWorkbench();
        const AppMode mode = (wb && wb->id() == QLatin1String("3D")) ? AppMode::Mode3D : AppMode::Mode2D;
        AboutDialog::showDialog(mode, m_self->workbenchWindow());
        return;
    }

    // 非窗口级命令：交由工作台 isCommandRegistered + OperationBus 分发
    //（此路径通常由 WorkbenchMenuManager::dispatchCommand 处理，MenuDispatcher 只做窗口级短路）
    SY_DEBUGF("[MenuDispatcher] unhandled commandId=%s (not window-level)", commandId.toUtf8().constData());
}

QString MenuDispatcher::commandIcon(const QString& commandId) const
{
    Q_UNUSED(commandId);
    return QString();
}