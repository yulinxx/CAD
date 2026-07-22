#include "StepExportWriter.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ExportResult StepExportWriter::write(const ExportContext& context,
    const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;
    Fio::WriteResult writeResult = fileIO.exportFile(
        context.targetPath.toUtf8().toStdString(),
        Fio::FileFormat::STEP,
        entities);

    if (!writeResult.success)
    {
        QString msg = QString::fromStdString(writeResult.errorMessage);
        SY_ERRORF("[StepExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    SY_INFOF("[StepExportWriter] Exported %d entities to STEP: %s",
        (int)entities.size(), context.targetPath.toUtf8().constData());

    return ExportResult::ok(
        QStringLiteral("STEP export successful"),
        static_cast<int>(entities.size()));
}
