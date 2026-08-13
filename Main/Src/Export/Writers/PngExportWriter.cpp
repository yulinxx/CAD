#include "PngExportWriter.h"

#include <vector>

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ExportResult PngExportWriter::write(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
{
    Fio::FileIOManager fileIO;

    std::vector<const Eg::SyEntity*> raw;
    raw.reserve(entities.size());
    for (const auto& entity : entities)
    {
        raw.push_back(entity.get());
    }

    char errBuf[1024] = { 0 };
    bool ok = fileIO.exportFile(context.targetPath.toUtf8().toStdString().c_str(),
        Fio::FileFormat::PNG,
        raw.data(),
        raw.size(),
        errBuf,
        sizeof(errBuf));

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        SY_ERRORF("[PngExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    SY_INFOF("[PngExportWriter] Exported %d entities to PNG: %s",
        (int)entities.size(),
        context.targetPath.toUtf8().constData());

    return ExportResult::ok(QStringLiteral("PNG export successful"), static_cast<int>(entities.size()));
}