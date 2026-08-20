#include "NativeExportWriter.h"

// 用于检测 3D 网格图元
#include "Engine3D/SyEntity/SyMeshEntity.h"

NativeExportWriter::NativeExportWriter()
    : ExportWriterBase(
          Fio::FileFormat::Native, { QStringLiteral("sy") }, QStringLiteral("SanYi Native"), QStringLiteral("sy"))
{
}

Fio::FileFormat NativeExportWriter::resolveFormat(const Fio::VecSyEntityPtr& entities) const
{
    // 检测是否包含 3D 网格图元，据此选择 3D/2D 原生格式
    for (const auto& entity : entities)
    {
        if (entity && entity->eType == Eg::EType::MESH)
        {
            return Fio::FileFormat::Native3D;
        }
    }
    return Fio::FileFormat::Native;
}

QString NativeExportWriter::successMessage() const
{
    return QStringLiteral("Native export successful");
}