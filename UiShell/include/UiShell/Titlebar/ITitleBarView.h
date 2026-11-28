#pragma once

#include <QString>
#include <QStringList>

#include "UiShell/Export.h"
#include "UiShell/Contract/IApiObject.h"
#include "UiShell/Contract/TitleBarSpec.h"

class QWidget;

namespace AA
{
    class ITitleBarHost;

    // 标题栏只读视图契约；具体 QWidget 由 shell 内部实现。
    // 所有窗口动作经 ITitleBarHost 转发，视图本身不访问 QMainWindow，也不依赖平台类型。
    class ITitleBarView : public IApiObject
    {
    public:
        virtual QWidget* widget() = 0;
        virtual const TitleBarSpec& spec() const = 0;
        virtual void setHost(ITitleBarHost* host) = 0;
        virtual void setMaximized(bool maximized) = 0;
        virtual bool isMaximizedShown() const = 0;
        virtual QString titleText() const = 0;
        virtual QStringList buttonIds() const = 0;

    protected:
        // 只能经 destroy()释放，禁止跨模块直接 delete
        ~ITitleBarView() override = default;

    public:

        // 需要内嵌菜单的标题栏同时实现 ITitleBarMenuHost；不实现时菜单保持独立区域。
    };

    UI_SHELL_API ITitleBarView* createTitleBar(
        const TitleBarSpec& spec, ITitleBarHost* host = nullptr, QWidget* parent = nullptr);
    // 所有权：传入 parent 时由父对象持有，不要 destroy()；parent 为空时调用方负责 destroy()。
}  // namespace AA
