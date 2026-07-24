#include "DxfImportReader.h"

#include <QFileInfo>

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ImportResult DxfImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    Fio::FileIOManager fileIO;
    Fio::ParseResult parseResult = fileIO.importFile(
        context.sourcePath.toUtf8().toStdString(),
        Fio::FileFormat::DXF,
        outEntities);

    if (!parseResult.success)
    {
        QString msg = QString::fromStdString(parseResult.errorMessage);
        SY_ERRORF("[DxfImportReader] Failed: %s", msg.toUtf8().constData());

        QStringList warns;
        for (const auto& w : parseResult.warnings)
            warns.append(QString::fromStdString(w));

        // 根据错误信息判断错误类型
        ImportErrorType errorType = ImportErrorType::ParseFailed;
        if (msg.contains(QStringLiteral("file not found"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("cannot open"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::FileNotFound;
        }
        else if (msg.contains(QStringLiteral("unit"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("scale"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::UnitIncompatible;
        }
        else if (msg.contains(QStringLiteral("coordinate"), Qt::CaseInsensitive) ||
            msg.contains(QStringLiteral("axis"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::CoordinateSystemIncompatible;
        }

        return ImportResult::fail(msg, errorType, warns);
    }

    QStringList warns;
    for (const auto& w : parseResult.warnings)
        warns.append(QString::fromStdString(w));

    return ImportResult::ok(
        QStringLiteral("DXF import successful"),
        static_cast<int>(outEntities.size()),
        static_cast<int>(parseResult.dxfLayers.size()),
        warns);
}