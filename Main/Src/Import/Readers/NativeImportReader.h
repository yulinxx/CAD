#pragma once

#include "../IImportReader.h"

/// Native (.sy / .syx) 格式导入读取器
/// 支持 SanYi CAD 原生 2D 格式 (.sy) 和 3D 格式 (.syx)
class NativeImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::Native;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("sy"), QStringLiteral("syx") };
    }

    QString formatName() const override
    {
        return QStringLiteral("SanYi Native");
    }

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};
