#include "SvgImportReader.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ImportResult SvgImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    Fio::FileIOManager fileIO;
    Fio::ParseResult parseResult = fileIO.importFile(
        context.sourcePath.toUtf8().toStdString(),
        Fio::FileFormat::SVG,
        outEntities);

    if (!parseResult.success)
    {
        QString msg = QString::fromStdString(parseResult.errorMessage);
        SY_ERRORF("[SvgImportReader] Failed: %s", msg.toUtf8().constData());

        QStringList warns;
        for (const auto& w : parseResult.warnings)
            warns.append(QString::fromStdString(w));

        return ImportResult::fail(msg, warns);
    }

    QStringList warns;
    for (const auto& w : parseResult.warnings)
        warns.append(QString::fromStdString(w));

    return ImportResult::ok(
        QStringLiteral("SVG import successful"),
        static_cast<int>(outEntities.size()),
        0, warns);
}
