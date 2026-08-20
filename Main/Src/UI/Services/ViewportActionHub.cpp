#include "ViewportActionHub.h"

#include "UI/Render/RenderViewport2D.h"

void ViewportActionHub::setViewport(RenderViewport2D* viewport)
{
    m_viewport = viewport;
}

void ViewportActionHub::clearViewport()
{
    m_viewport = nullptr;
}

void ViewportActionHub::handle(const QString& action)
{
    if (!m_viewport)
    {
        return;
    }

    if (action == QLatin1String("zoom_in"))
    {
        m_viewport->zoomIn();
    }
    else if (action == QLatin1String("zoom_out"))
    {
        m_viewport->zoomOut();
    }
    else if (action == QLatin1String("zoom_fit"))
    {
        m_viewport->zoomToFit();
    }
    else if (action == QLatin1String("zoom_selection"))
    {
        m_viewport->zoomToSelection();
    }
    else if (action == QLatin1String("reset"))
    {
        m_viewport->resetView();
    }
    else if (action == QLatin1String("pan"))
    {
        m_viewport->setPanModeEnabled(!m_viewport->isPanModeEnabled());
    }
}