#include "BmpExportWriter.h"

#include <vector>

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ExportResult BmpExportWriter::write(const ExportContext& context, const Fio::VecSyEntityPtr& entities)
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
        Fio::FileFormat::BMP,
        raw.data(),
        raw.size(),
        errBuf,
        sizeof(errBuf));

    if (!ok)
    {
        QString msg = QString::fromUtf8(errBuf);
        SY_ERRORF("[BmpExportWriter] Failed: %s", msg.toUtf8().constData());
        return ExportResult::fail(msg);
    }

    SY_INFOF("[BmpExportWriter] Exported %d entities to BMP: %s",
        (int)entities.size(),
        context.targetPath.toUtf8().constData());

    return ExportResult::ok(QStringLiteral("BMP export successful"), static_cast<int>(entities.size()));
}