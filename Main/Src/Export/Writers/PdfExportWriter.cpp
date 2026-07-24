#include "PdfExportWriter.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ExportResult PdfExportWriter::write(const ExportContext& context,
    const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;
    Fio::WriteResult writeResult = fileIO.exportFile(
        context.targetPath.toUtf8().toStdString(),
        Fio::FileFormat::PDF,
        entities);

    if (!writeResult.success)
    {
        QString msg = QString::fromStdString(writeResult.errorMessage);
        SY_ERRORF("[PdfExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    SY_INFOF("[PdfExportWriter] Exported %d entities to PDF: %s",
        (int)entities.size(), context.targetPath.toUtf8().constData());

    return ExportResult::ok(
        QStringLiteral("PDF export successful"),
        static_cast<int>(entities.size()));
}