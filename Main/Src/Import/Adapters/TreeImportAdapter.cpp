#include "TreeImportAdapter.h"

void TreeImportAdapter::setRebuildCallback(std::function<void()> callback)
{
    m_rebuildCallback = std::move(callback);
}

void TreeImportAdapter::rebuild()
{
    if (m_rebuildCallback)
    {
        m_rebuildCallback();
    }
}