#pragma once

#include "../IImportReader.h"

/// PLT (HPGL) 格式导入读取器
class PltImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::PLT;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("plt"), QStringLiteral("hpgl") };
    }

    QString formatName() const override
    {
        return QStringLiteral("PLT");
    }

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
