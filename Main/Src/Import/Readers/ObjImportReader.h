#pragma once

#include "../IImportReader.h"

/// OBJ 格式导入读取器
/// 注意：当前版本 OBJ 导入依赖 Engine3D/ObjLoader，FileIO 暂不直接支持
class ObjImportReader : public IImportReader
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

    ImportResult read(const ImportContext& context,
        Fio::VecSyEntityPtr& outEntities) override;
};
