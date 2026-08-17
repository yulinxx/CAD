#include "NativeExportWriter.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

// 用于检测 3D 网格图元
#include "Engine3D/SyEntity/SyMeshEntity.h"

ExportResult NativeExportWriter::write(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;

    // 检测是否包含 3D 网格图元
    bool hasMesh3D = false;
    for (const auto& entity : entities)
    {
        if (entity && entity->eType == Eg::EType::MESH)
        {
            hasMesh3D = true;
            break;
        }
    }

    // 根据图元类型选择文件格式
    const Fio::FileFormat format = hasMesh3D ? Fio::FileFormat::Native3D : Fio::FileFormat::Native;

    std::vector<const Eg::SyEntity*> raw;
    raw.reserve(entities.size());
    for (const auto& entity : entities)
    {
        raw.push_back(entity.get());
    }

    char errBuf[1024] = { 0 };
    bool ok = fileIO.exportFile(context.targetPath.toUtf8().toStdString().c_str(),
        format,
        raw.data(),
        raw.size(),
        errBuf,
        sizeof(errBuf));

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        SY_ERRORF("[NativeExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    const char* fmtName = hasMesh3D ? ".syx (3D)" : ".sy (2D)";
    SY_INFOF("[NativeExportWriter] Exported %d entities to native %s: %s",
        (int)entities.size(),
        fmtName,
        context.targetPath.toUtf8().constData());

    return ExportResult::ok(QStringLiteral("Native export successful"), static_cast<int>(entities.size()));
}
