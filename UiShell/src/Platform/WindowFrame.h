#pragma once

#include <QRect>
#include <QSize>
#include <Qt>

#include <memory>

class QWidget;

namespace AA
{
    // 平台窗口框架：Windows 自绘窗口（无边框 + DWM 命中），其它平台 Qt 原生窗口。
    class IWindowFrame
    {
    public:
        virtual ~IWindowFrame() = default;

    public:
        virtual void attach(QWidget* window) = 0;

        // 非 Windows 平台由该控件提供边缘鼠标事件以触发 startSystemResize
        virtual void setResizeBorderWidget(QWidget* widget) = 0;

        virtual void setMinimumSize(const QSize& size) = 0;

        // Windows 使用原生命中，返回 0；其它返回建议的可缩放边距
        virtual int resizeBorderWidth() const = 0;

        // 窗口移动
        virtual bool startSystemMove() = 0;
        virtual bool startSystemResize(Qt::Edges edges) = 0;

        // 读取原生窗口当前信息
        virtual QRect nativeFrameGeometry() const = 0;

        // 刷新
        virtual void refreshTargetHwnds() = 0;
    };

    std::unique_ptr<IWindowFrame> createWindowFrame();
}  // namespace AA
