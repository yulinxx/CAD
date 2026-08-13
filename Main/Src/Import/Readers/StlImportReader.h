#pragma once

#include "../IImportReader.h"

/**
 * @brief STL 格式导入读取器
 * 使用 Engine3D::StlLoader 加载 STL 网格数据，
 * 转换为 SyMeshEntity（通过 SyEntity 基类指针）返回。
 */
class StlImportReader : public IImportReader
{
public:
    Fio::FileFormat format() const override
    {
        return Fio::FileFormat::STL;
    }

    QStringList supportedExtensions() const override
    {
        return { QStringLiteral("stl") };
    }

    QString formatName() const override
    {
        return QStringLiteral("STL");
    }

    ImportResult read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities) override;
};