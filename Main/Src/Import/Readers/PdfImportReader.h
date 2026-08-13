#pragma once

#include "../IImportReader.h"

/// PDF 格式导入读取器
class PdfImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::PDF;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("pdf") };
    }

    QString formatName() const override
    {
        return QStringLiteral("PDF");
    }

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
