/**
 * @file MenuDispatcher.h
 * @brief 窗口级命令分发器（从 WorkbenchMenuManager 拆分）
 *
 * 负责：工作台切换 / 主题 / 语言 / help.about 四类窗口级命令的短路分发。
 * 这些命令刻意不进命令目录、不进命令总线，直接由主窗口/工作台处理。
 *
 * 单一真相来源：isWindowLevelCommand() 判定名单，供 isCommandRegistered、
 * rebuildMenusFromConfig 的 commandAvailable、契约测试/自检复用。
 */
#pragma once

#include <QObject>
#include <QString>
#include <functional>

#include "UI/ClientConfig/IUiCommandDispatcher.h"

class WorkbenchWindow;
class UiWorkbench;
class UiStateCenter;
class WorkbenchMenuManager;  // 前置声明，避免循环依赖
enum class AppMode;

class MenuDispatcher : public QObject, public IUiCommandDispatcher
{
    Q_OBJECT

public:
    explicit MenuDispatcher(QObject* parent = nullptr);

    // 依赖注入（由 WorkbenchMenuManager 在 commandDispatcher() 时同步）
    void setSelf(WorkbenchMenuManager* self);
    void setWorkbench(class UiWorkbench* workbench);
    void setWorkbenchWindow(WorkbenchWindow* window);
    void setStateCenter(class UiStateCenter* stateCenter);

    // 命令分发入口
    void dispatch(const QString& commandId, const QVariantMap& params = {}) override;

    // 接口实现
    bool isCommandRegistered(const QString& commandId) const override;
    QString commandIcon(const QString& commandId) const override;

    // 单一真相：窗口级命令名单（工作台切换 / 主题 / 语言 / help.about）
    static bool isWindowLevelCommand(const QString& commandId);
    static bool isWorkbenchSwitchCommand(const QString& commandId);
    static bool isThemeCommand(const QString& commandId);
    static bool isLanguageCommand(const QString& commandId);

private:
    WorkbenchMenuManager* m_self{ nullptr };
    class UiWorkbench* m_workbench{ nullptr };
    WorkbenchWindow* m_workbenchWindow{ nullptr };
    class UiStateCenter* m_stateCenter{ nullptr };
};