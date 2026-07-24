#pragma once

#include "../IImportReader.h"

/// SVG 格式导入读取器
class SvgImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::SVG;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("svg"), QStringLiteral("svgz") };
    }

    QString formatName() const override
    {
        return QStringLiteral("SVG");
    }

    ImportResult read(const ImportContext& context,
        Fio::VecSyEntityPtr& outEntities) override;
};
