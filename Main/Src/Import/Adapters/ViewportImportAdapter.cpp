#include "ViewportImportAdapter.h"

void ViewportImportAdapter::setFitToContentCallback(std::function<void()> callback)
{
    m_fitCallback = std::move(callback);
}

void ViewportImportAdapter::setCenterOnContentCallback(std::function<void()> callback)
{
    m_centerCallback = std::move(callback);
}

void ViewportImportAdapter::setRefreshCallback(std::function<void()> callback)
{
    m_refreshCallback = std::move(callback);
}

void ViewportImportAdapter::fitToContent()
{
    if (m_fitCallback)
    {
        m_fitCallback();
    }
}

void ViewportImportAdapter::centerOnContent()
{
    if (m_centerCallback)
    {
        m_centerCallback();
    }
}

void ViewportImportAdapter::refresh()
{
    if (m_refreshCallback)
    {
        m_refreshCallback();
    }
}