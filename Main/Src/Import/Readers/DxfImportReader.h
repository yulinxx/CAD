#pragma once

#include "../IImportReader.h"

/// DXF 格式导入读取器
class DxfImportReader : public IImportReader
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

    ImportResult read(const ImportContext& context,
        Fio::VecSyEntityPtr& outEntities) override;
};
