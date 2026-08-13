#pragma once

#include "../IExportWriter.h"

/// DXF 格式导出写入器
class DxfExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::DXF;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("dxf") };
    }

    QString formatName() const override
    {
        return QStringLiteral("DXF");
    }

    QString defaultExtension() const override
    {
        return QStringLiteral("dxf");
    }

    ExportResult write(const ExportContext& context, const Fio::VecSyEntityPtr& entities) override;
};
