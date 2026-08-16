#pragma once

#include "../IImportReader.h"

/// Unigraphics / Siemens NX (UG) 格式导入读取器
///
/// 支持通过 IGES（.igs / .iges）中性交换格式导入几何。
/// 注意：NX 原生 .prt 为私有二进制格式，需 NX Open API 才能解析，
/// 本读取器仅覆盖 IGES 交换格式子集（基础曲线/圆/圆弧/折线等）。
class UgImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::UG;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("igs"), QStringLiteral("iges") };
    }

    QString formatName() const override
    {
        return QStringLiteral("Unigraphics (IGES)");
    }

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
