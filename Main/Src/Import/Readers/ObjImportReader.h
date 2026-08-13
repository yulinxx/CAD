#pragma once

#include "../IImportReader.h"

/**
 * @brief OBJ 格式导入读取器
 * 使用 Engine3D::ObjLoader 加载 OBJ 网格数据，
 * 转换为 SyMeshEntity（通过 SyEntity 基类指针）返回。
 */
class ObjImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::OBJ;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("obj") };
    }

    QString formatName() const override
    {
        return QStringLiteral("OBJ");
    }

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};