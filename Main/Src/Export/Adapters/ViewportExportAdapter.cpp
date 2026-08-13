#include "ViewportExportAdapter.h"

#include <QImage>

void ViewportExportAdapter::setCaptureCallback(std::function<QImage()> callback)
{
    m_captureCallback = std::move(callback);
}

void ViewportExportAdapter::setRenderPreviewCallback(std::function<void()> callback)
{
    m_renderPreviewCallback = std::move(callback);
}

QImage ViewportExportAdapter::capture()
{
    if (m_captureCallback)
    {
        return m_captureCallback();
    }
    return QImage();
}

void ViewportExportAdapter::renderPreview()
{
    if (m_renderPreviewCallback)
    {
        m_renderPreviewCallback();
    }
}