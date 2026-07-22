#include "SvgExportWriter.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ExportResult SvgExportWriter::write(const ExportContext& context,
    const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;
    Fio::WriteResult writeResult = fileIO.exportFile(
        context.targetPath.toUtf8().toStdString(),
        Fio::FileFormat::SVG,
        entities);

    if (!writeResult.success)
    {
        QString msg = QString::fromStdString(writeResult.errorMessage);
        SY_ERRORF("[SvgExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    SY_INFOF("[SvgExportWriter] Exported %d entities to SVG: %s",
        (int)entities.size(), context.targetPath.toUtf8().constData());

    return ExportResult::ok(
        QStringLiteral("SVG export successful"),
        static_cast<int>(entities.size()));
}
