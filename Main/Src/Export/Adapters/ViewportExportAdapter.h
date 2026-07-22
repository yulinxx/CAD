#pragma once

#include <functional>

class QImage;

/// 视口导出适配器：捕获视口内容用于导出预览或位图输出
class ViewportExportAdapter
{
public:
    ViewportExportAdapter() = default;

    /// 设置视口捕获回调
    /// @param callback 返回当前视口图像的 QImage
    void setCaptureCallback(std::function<QImage()> callback);

    /// 设置视口渲染预览回调
    void setRenderPreviewCallback(std::function<void()> callback);

    /// 捕获当前视口内容
    QImage capture();

    /// 触发视口渲染预览
    void renderPreview();

private:
    /// 视口捕获回调
    std::function<QImage()> m_captureCallback;
    /// 渲染预览回调
    std::function<void()> m_renderPreviewCallback;
};
