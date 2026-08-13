#include "PropertyImportAdapter.h"

void PropertyImportAdapter::setRefreshSelectionCallback(std::function<void()> callback)
{
    m_refreshCallback = std::move(callback);
}

void PropertyImportAdapter::refreshSelection()
{
    if (m_refreshCallback)
    {
        m_refreshCallback();
    }
}