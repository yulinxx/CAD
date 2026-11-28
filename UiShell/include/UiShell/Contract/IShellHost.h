#pragma once

#include <QJsonObject>
#include <QPoint>
#include <QString>

#include "UiShell/Contract/DialogRegistry.h"

class QAction;
class QWidget;

namespace AA
{
    // 面板宿主
    class IShellHost
    {
    protected:
        virtual ~IShellHost() = default;

    public:
        // 执行命令
        virtual void invoke(const QString& commandId) = 0;
        // 带参数执行
        virtual void invokeWithArgs(const QString& commandId, const QJsonObject& args) = 0;

        // 命令 QAction（菜单、工具栏、快捷键、面板按钮共用）
        virtual QAction* commandAction(const QString& commandId) = 0;

        // 重新查询所有命令的可用/勾选状态
        virtual void refreshCommands() = 0;

        // 右键菜单：控件只报告“哪个上下文 + 全局坐标”，菜单项由外壳按命令动作池构建。
        // 默认空实现让既有宿主实现无需立即改造；生产宿主应覆盖此入口。
        virtual void showContextMenu(const QString& contextMenuId, const QPoint& globalPos, QWidget* anchor)
        {
            Q_UNUSED(contextMenuId);
            Q_UNUSED(globalPos);
            Q_UNUSED(anchor);
        }

        // 激活面板
        virtual bool activatePanel(const QString& panelId) = 0;

        // 显示提示文字
        virtual void showMessage(const QString& text) = 0;

        // 打开对话框
        virtual DialogResult openDialog(const QString& dialogId, QWidget* parent, const QJsonObject& params = {}) = 0;
    };
}  // namespace AA
