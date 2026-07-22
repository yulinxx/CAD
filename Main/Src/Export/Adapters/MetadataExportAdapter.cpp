#include "MetadataExportAdapter.h"

void MetadataExportAdapter::setApplyCallback(
    std::function<void(const QString&, const QString&, int)> callback)
{
    m_applyCallback = std::move(callback);
}

void MetadataExportAdapter::apply(const QString& targetPath,
    const QString& format, int entityCount)
{
    if (m_applyCallback)
        m_applyCallback(targetPath, format, entityCount);
}
