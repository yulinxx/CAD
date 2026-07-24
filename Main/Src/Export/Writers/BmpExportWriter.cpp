#include "BmpExportWriter.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ExportResult BmpExportWriter::write(const ExportContext& context,
    const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;
    Fio::WriteResult writeResult = fileIO.exportFile(
        context.targetPath.toUtf8().toStdString(),
        Fio::FileFormat::BMP,
        entities);

    if (!writeResult.success)
    {
        QString msg = QString::fromStdString(writeResult.errorMessage);
        SY_ERRORF("[BmpExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    SY_INFOF("[BmpExportWriter] Exported %d entities to BMP: %s",
        (int)entities.size(), context.targetPath.toUtf8().constData());

    return ExportResult::ok(
        QStringLiteral("BMP export successful"),
        static_cast<int>(entities.size()));
}