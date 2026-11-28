#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "UiShell/Export.h"
#include "UiShell/Assembly/ShellServices.h"
#include "UiShell/Contract/IApiObject.h"
#include "UiShell/Contract/IShellHost.h"
#include "UiShell/Contract/Diagnostic.h"

class QMainWindow;
class QMenu;
class QWidget;

namespace AA
{
    class IRegionContainer;

    // 装配运行接口
    class IShellRuntime : public IShellHost, public IApiObject
    {
    public:
        virtual bool loadFromJson(const QByteArray& utf8Json) = 0;
        virtual bool loadFromFile(const QString& filePath) = 0;

        virtual QWidget* shell() const = 0;
        virtual QWidget* takeShell() = 0;

        virtual bool attachWindow(QMainWindow* window) = 0;

        // 切換语言
        virtual void switchLanguage(const QString& language) = 0;
        virtual void retranslate() = 0;

        // 重建中央视图：客户切换工作台形态后请求外壳重新向 ICenterViewProvider 取部件。
        // 外壳只认识通用契约，不感知具体 2D/3D 实现。
        virtual void refreshCenterView() = 0;

        // 切换皮肤
        virtual void switchTheme(const QString& themeId) = 0;
        virtual QString theme() const = 0;

        // profile 解析与诊断
        virtual const QList<Diagnostic>& diagnostics() const = 0;
        virtual QString lastError() const = 0;
        virtual QString lastMessage() const = 0;

        virtual IRegionContainer* regionContainer(const QString& regionId) const = 0;
        virtual IRegionContainer* regionContainerForPanel(const QString& panelId) const = 0;

        // 右键菜单数据 → 共享命令动作的 QMenu；调用方负责在合适位置 exec() 或 show()。
        // 默认返回空，兼容不提供右键能力的宿主实现。
        virtual QMenu* createContextMenu(const QString& contextMenuId, QWidget* parent = nullptr)
        {
            Q_UNUSED(contextMenuId);
            Q_UNUSED(parent);
            return nullptr;
        }

    protected:
        // 只能 destroy释放，禁止跨模块 delete
        ~IShellRuntime() override = default;
    };

    UI_SHELL_API IShellRuntime* createShellRuntime(const ShellServices& services);
}  // namespace AA
