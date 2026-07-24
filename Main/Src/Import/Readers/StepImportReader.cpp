#include "StepImportReader.h"

#include "FileIO/FileIOManager.h"
#include "Log/SyLogger.h"

ImportResult StepImportReader::read(const ImportContext& context,
    Fio::VecSyEntityPtr& outEntities)
{
    Fio::FileIOManager fileIO;
    Fio::ParseResult parseResult = fileIO.importFile(
        context.sourcePath.toUtf8().toStdString(),
        Fio::FileFormat::STEP,
        outEntities);

    if (!parseResult.success)
    {
        QString msg = QString::fromStdString(parseResult.errorMessage);
        SY_ERRORF("[StepImportReader] Failed: %s", msg.toUtf8().constData());

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
        else if (msg.contains(QStringLiteral("unit"), Qt::CaseInsensitive))
        {
            errorType = ImportErrorType::UnitIncompatible;
        }

        return ImportResult::fail(msg, errorType, warns);
    }

    QStringList warns;
    for (const auto& w : parseResult.warnings)
        warns.append(QString::fromStdString(w));

    return ImportResult::ok(
        QStringLiteral("STEP import successful"),
        static_cast<int>(outEntities.size()),
        0, warns);
}