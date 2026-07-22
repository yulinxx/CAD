#pragma once

#include "../IExportWriter.h"

/// BMP 格式导出写入器
class BmpExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override { return Fio::FileFormat::BMP; }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("bmp") };
    }

    QString formatName() const override { return QStringLiteral("BMP"); }

    QString defaultExtension() const override { return QStringLiteral("bmp"); }

    ExportResult write(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities) override;
};
