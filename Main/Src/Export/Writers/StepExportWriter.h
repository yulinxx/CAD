#pragma once

#include "../IExportWriter.h"

/// STEP 格式导出写入器
class StepExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override { return Fio::FileFormat::STEP; }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("stp"), QStringLiteral("step") };
    }

    QString formatName() const override { return QStringLiteral("STEP"); }

    QString defaultExtension() const override { return QStringLiteral("step"); }

    ExportResult write(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities) override;
};
