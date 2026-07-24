#pragma once

#include "../IExportWriter.h"

/// OBJ 格式导出写入器
class ObjExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::Unknown;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("obj") };
    }

    QString formatName() const override
    {
        return QStringLiteral("OBJ");
    }

    QString defaultExtension() const override
    {
        return QStringLiteral("obj");
    }

    ExportResult write(const ExportContext& context,
        const Fio::VecSyEntityPtr& entities) override;
};
