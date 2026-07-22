#pragma once

#include "../IExportWriter.h"

/// PDF 格式导出写入器
class PdfExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override { return Fio::FileFormat::PDF; }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("pdf") };
    }

    QString formatName() const override { return QStringLiteral("PDF"); }

    QString defaultExtension() const override { return QStringLiteral("pdf"); }

    ExportResult write(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities) override;
};
