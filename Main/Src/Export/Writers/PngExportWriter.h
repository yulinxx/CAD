#pragma once

#include "../IExportWriter.h"

/// PNG 格式导出写入器
class PngExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::PNG;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("png") };
    }

    QString formatName() const override
    {
        return QStringLiteral("PNG");
    }

    QString defaultExtension() const override
    {
        return QStringLiteral("png");
    }

    ExportResult write(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities) override;
};
