#include "StlImportReader.h"

#include "Engine3D/Loader/StlLoader.h"
#include "Log/SyLogger.h"

StlImportReader::StlImportReader()
    : ImportReaderBase(Fio::FileFormat::STL, { QStringLiteral("stl") }, QStringLiteral("STL"))
{
}

ImportResult StlImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{
    SY_INFOF("[StlImportReader] read START: path=%s", context.sourcePath.toUtf8().constData());

    // 主链路：中立 IR 导入（parseToIR → FioEntityConverter）
    ImportResult result;
    QString errMsg;
    if (tryImportViaIR(context, Fio::FileFormat::STL, outEntities, false, &result, &errMsg))
    {
        return result;
    }
    SY_WARNF("[StlImportReader] IR path unavailable, falling back to legacy: %s",
        errMsg.isEmpty() ? "no entities" : errMsg.toUtf8().constData());

    // 回退路径：旧版直接 StlLoader
    std::string error;
    auto mesh = Eg::StlLoader::load(context.sourcePath.toUtf8().toStdString(), error);

    if (!mesh)
    {
        QString msg = QString::fromStdString(error);
        SY_ERRORF("[StlImportReader] Failed to load STL file: %s (path=%s)",
            msg.toUtf8().constData(),
            context.sourcePath.toUtf8().constData());
        return ImportResult::fail(msg, ImportErrorType::ParseFailed);
    }

    SY_INFOF("[StlImportReader] STL loaded successfully (legacy): %s, triangles=%zu, verts=%zu",
        mesh->name(),
        mesh->triangleCount(),
        mesh->vertices.size());

    outEntities.push_back(std::move(mesh));
    return ImportResult::ok(QStringLiteral("STL import successful: 1 mesh entity"), 1);
}