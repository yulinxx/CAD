#pragma once

#include "../IExportWriter.h"

/// SVG 格式导出写入器
class SvgExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::SVG;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("svg") };
    }

    QString formatName() const override
    {
        return QStringLiteral("SVG");
    }

    QString defaultExtension() const override
    {
        return QStringLiteral("svg");
    }

    ExportResult write(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities) override;
};
