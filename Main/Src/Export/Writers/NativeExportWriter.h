#pragma once

#include "../IExportWriter.h"

/// 原生 (.sy) 格式导出写入器 —— 用于 File_Save 的默认保存格式
class NativeExportWriter : public IExportWriter
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::Native;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("sy") };
    }

    QString formatName() const override
    {
        return QStringLiteral("SanYi Native");
    }

    QString defaultExtension() const override
    {
        return QStringLiteral("sy");
    }

    ExportResult write(const ExportContext& context, const Fio::VecSyEntityPtr& entities) override;
};
