#include "UgImportReader.h"

#include "Log/SyLogger.h"

UgImportReader::UgImportReader()
    : ImportReaderBase(
          Fio::FileFormat::UG, { QStringLiteral("igs"), QStringLiteral("iges") }, QStringLiteral("Unigraphics (IGES)"))
{
}

QString UgImportReader::successMessage(Fio::FileFormat /*format*/) const
{
    return QStringLiteral("UG/IGES import successful");
}

QString UgImportReader::noEntitiesMessage() const
{
    return QStringLiteral("UG/IGES import produced no entities");
}

void UgImportReader::decorateError(QString& msg, const ImportContext& context) const
{
    // 文件是 NX 原生 .prt 时给出更明确的说明
    if (context.sourcePath.endsWith(QStringLiteral(".prt"), Qt::CaseInsensitive))
    {
        msg = QStringLiteral("NX native .prt is a private binary format not directly supported.\n"
                             "Please export the model as IGES (.igs/.iges) from Siemens NX and import that file.");
    }
}

ImportResult UgImportReader::read(const ImportContext& context, Fio::VecSyEntityPtr& outEntities)
{

    // 走 FileIO 的 UgParser（IGES 子集解析），将 IGES 图元转换为中立 IR
    return readViaIR(context, Fio::FileFormat::UG, outEntities, true);
}