#pragma once

#include <QRect>
#include <QSize>
#include <Qt>

#include <memory>

#include "UiShell/Export.h"

class QWidget;

namespace AA
{
    class IWindowFrame
    {
    public:
        virtual ~IWindowFrame() = default;

    public:
        // 在窗口 show() 之前调用：去除系统标题栏、保留缩放/贴靠特性、设置最小尺寸。
        virtual void attach(QWidget* window) = 0;

        // 非 Windows 平台由该控件提供边缘鼠标事件以触发 startSystemResize。
        virtual void setResizeBorderWidget(QWidget* widget) = 0;

        virtual void setMinimumSize(const QSize& size) = 0;

        // Windows 返回 0；其它平台返回建议的可缩放边距（像素）。
        virtual int resizeBorderWidth() const = 0;

        // 发起系统级窗口移动；自绘标题栏经 ITitleBarHost::requestDrag() 间接调用。
        virtual bool startSystemMove() = 0;
        virtual bool startSystemResize(Qt::Edges edges) = 0;

        // 立即读取原生窗口框体几何；查询失败或未附加时返回空 QRect。
        virtual QRect nativeFrameGeometry() const = 0;

        // 刷新本进程内需接收原生消息的顶层窗口集合。
        virtual void refreshTargetHwnds() = 0;
    };

    UI_SHELL_API std::unique_ptr<IWindowFrame> createWindowFrame();
}  // namespace AA
