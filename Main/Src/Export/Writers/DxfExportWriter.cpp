#include "DxfExportWriter.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ExportResult DxfExportWriter::write(const ExportContext& context,
    const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;
    Fio::WriteResult writeResult = fileIO.exportFile(
        context.targetPath.toUtf8().toStdString(),
        Fio::FileFormat::DXF,
        entities);

    if (!writeResult.success)
    {
        QString msg = QString::fromStdString(writeResult.errorMessage);
        SY_ERRORF("[DxfExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    SY_INFOF("[DxfExportWriter] Exported %d entities to DXF: %s",
        (int)entities.size(), context.targetPath.toUtf8().constData());

    return ExportResult::ok(
        QStringLiteral("DXF export successful"),
        static_cast<int>(entities.size()));
}
